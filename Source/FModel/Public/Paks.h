#pragma once

#include "IPlatformFilePak.h"

/**
 * Class to handle correctly reading from a compressed file within a compressed package
 */
class FPakSimpleEncryption
{
public:
	enum
	{
		Alignment = FAES::AESBlockSize,
	};

	static FORCEINLINE int64 AlignReadRequest(int64 Size)
	{
		return Align(Size, Alignment);
	}

	static FORCEINLINE void DecryptBlock(void* Data, int64 Size, const FGuid& EncryptionKeyGuid)
	{
		FPakPlatformFile::GetPakCustomEncryptionDelegate().Execute((uint8*)Data, Size, EncryptionKeyGuid);
	}
};

struct FCompressionScratchBuffers
{
	FCompressionScratchBuffers()
		: TempBufferSize(0)
		, ScratchBufferSize(0)
		, LastPakEntryOffset(-1)
		, LastDecompressedBlock(0xFFFFFFFF)
		, Next(nullptr)
	{}

	int64				TempBufferSize;
	TUniquePtr<uint8[]>	TempBuffer;
	int64				ScratchBufferSize;
	TUniquePtr<uint8[]>	ScratchBuffer;

	int64 LastPakEntryOffset;
	FSHAHash LastPakIndexHash;
	uint32 LastDecompressedBlock;

	FCompressionScratchBuffers* Next;

	void EnsureBufferSpace(int64 CompressionBlockSize, int64 ScrachSize)
	{
		if (TempBufferSize < CompressionBlockSize)
		{
			TempBufferSize = CompressionBlockSize;
			TempBuffer = MakeUnique<uint8[]>(TempBufferSize);
		}
		if (ScratchBufferSize < ScrachSize)
		{
			ScratchBufferSize = ScrachSize;
			ScratchBuffer = MakeUnique<uint8[]>(ScratchBufferSize);
		}
	}
};

/**
 * Thread local class to manage working buffers for file compression
 */
class FCompressionScratchBuffersStack : public TThreadSingleton<FCompressionScratchBuffersStack>
{
public:
	FCompressionScratchBuffersStack()
		: bFirstInUse(false)
		, RecursionList(nullptr)
	{}

private:
	FCompressionScratchBuffers* Acquire()
	{
		if (!bFirstInUse)
		{
			bFirstInUse = true;
			return &First;
		}
		FCompressionScratchBuffers* Top = new FCompressionScratchBuffers;
		Top->Next = RecursionList;
		RecursionList = Top;
		return Top;
	}

	void Release(FCompressionScratchBuffers* Top)
	{
		check(bFirstInUse);
		if (!RecursionList)
		{
			check(Top == &First);
			bFirstInUse = false;
		}
		else
		{
			check(Top == RecursionList);
			RecursionList = Top->Next;
			delete Top;
		}
	}

	bool bFirstInUse;
	FCompressionScratchBuffers First;
	FCompressionScratchBuffers* RecursionList;

	friend class FScopedCompressionScratchBuffers;
};

class FScopedCompressionScratchBuffers
{
public:
	FScopedCompressionScratchBuffers()
		: Inner(FCompressionScratchBuffersStack::Get().Acquire())
	{}

	~FScopedCompressionScratchBuffers()
	{
		FCompressionScratchBuffersStack::Get().Release(Inner);
	}

	FCompressionScratchBuffers* operator->() const
	{
		return Inner;
	}

private:
	FCompressionScratchBuffers* Inner;
};

/**
 * Class to handle correctly reading from a compressed file within a pak
 */
template< typename EncryptionPolicy = FPakNoEncryption >
class FPakCompressedReaderPolicy
{
public:
	class FPakUncompressTask : public FNonAbandonableTask
	{
	public:
		uint8*				UncompressedBuffer;
		int32				UncompressedSize;
		uint8*				CompressedBuffer;
		int32				CompressedSize;
		FName				CompressionFormat;
		void*				CopyOut;
		int64				CopyOffset;
		int64				CopyLength;
		FGuid				EncryptionKeyGuid;

		void DoWork()
		{
			// Decrypt and Uncompress from memory to memory.
			int64 EncryptionSize = EncryptionPolicy::AlignReadRequest(CompressedSize);
			EncryptionPolicy::DecryptBlock(CompressedBuffer, EncryptionSize, EncryptionKeyGuid);
			FCompression::UncompressMemory(CompressionFormat, UncompressedBuffer, UncompressedSize, CompressedBuffer, CompressedSize);
			if (CopyOut)
			{
				FMemory::Memcpy(CopyOut, UncompressedBuffer + CopyOffset, CopyLength);
			}
		}

		FORCEINLINE TStatId GetStatId() const
		{
			// TODO: This is called too early in engine startup.
			return TStatId();
			//RETURN_QUICK_DECLARE_CYCLE_STAT(FPakUncompressTask, STATGROUP_ThreadPoolAsyncTasks);
		}
	};

	FPakCompressedReaderPolicy(const FPakFile& InPakFile, const FPakEntry& InPakEntry, TAcquirePakReaderFunction& InAcquirePakReader)
		: PakFile(InPakFile)
		, PakEntry(InPakEntry)
		, AcquirePakReader(InAcquirePakReader)
	{
	}

	~FPakCompressedReaderPolicy()
	{
	}

	/** Pak file that own this file data */
	const FPakFile&		PakFile;
	/** Pak file entry for this file. */
	FPakEntry			PakEntry;
	/** Function that gives us an FArchive to read from. The result should never be cached, but acquired and used within the function doing the serialization operation */
	TAcquirePakReaderFunction AcquirePakReader;

	FORCEINLINE int64 FileSize() const
	{
		return PakEntry.UncompressedSize;
	}

	void Serialize(int64 DesiredPosition, void* V, int64 Length)
	{
		const int32 CompressionBlockSize = PakEntry.CompressionBlockSize;
		uint32 CompressionBlockIndex = DesiredPosition / CompressionBlockSize;
		uint8* WorkingBuffers[2];
		int64 DirectCopyStart = DesiredPosition % PakEntry.CompressionBlockSize;
		FAsyncTask<FPakUncompressTask> UncompressTask;
		FScopedCompressionScratchBuffers ScratchSpace;
		bool bStartedUncompress = false;

		FName CompressionMethod = PakFile.GetInfo().GetCompressionMethod(PakEntry.CompressionMethodIndex);
		checkf(FCompression::IsFormatValid(CompressionMethod),
			TEXT("Attempting to use compression format %s when loading a file from a .pak, but that compression format is not available.\n")
			TEXT("If you are running a program (like UnrealPak) you may need to pass the .uproject on the commandline so the plugin can be found.\n"),
			TEXT("It's also possible that a necessary compression plugin has not been loaded yet, and this file needs to be forced to use zlib compression.\n")
			TEXT("Unfortunately, the code that can check this does not have the context of the filename that is being read. You will need to look in the callstack in a debugger.\n")
			TEXT("See ExtensionsToNotUsePluginCompression in [Pak] section of Engine.ini to add more extensions."),
			*CompressionMethod.ToString(), TEXT("Unknown"));

		int64 WorkingBufferRequiredSize = FCompression::GetMaximumCompressedSize(CompressionMethod,CompressionBlockSize);
		if ( CompressionMethod != NAME_Oodle )
		{
			// an amount to extra allocate, in case one block's compressed size is bigger than GetMaximumCompressedSize
			// @todo this should not be needed, can it be removed?
			float SlopMultiplier = 1.1f;
			WorkingBufferRequiredSize = (int64)( WorkingBufferRequiredSize * SlopMultiplier );
		}

		WorkingBufferRequiredSize = EncryptionPolicy::AlignReadRequest(WorkingBufferRequiredSize);
		const bool bExistingScratchBufferValid = ScratchSpace->TempBufferSize >= CompressionBlockSize;
		ScratchSpace->EnsureBufferSpace(CompressionBlockSize, WorkingBufferRequiredSize * 2);
		WorkingBuffers[0] = ScratchSpace->ScratchBuffer.Get();
		WorkingBuffers[1] = ScratchSpace->ScratchBuffer.Get() + WorkingBufferRequiredSize;

		FSharedPakReader PakReader = AcquirePakReader();

		while (Length > 0)
		{
			const FPakCompressedBlock& Block = PakEntry.CompressionBlocks[CompressionBlockIndex];
			int64 Pos = CompressionBlockIndex * CompressionBlockSize;
			int64 CompressedBlockSize = Block.CompressedEnd - Block.CompressedStart;
			int64 UncompressedBlockSize = FMath::Min<int64>(PakEntry.UncompressedSize - Pos, PakEntry.CompressionBlockSize);

			if (CompressedBlockSize > UncompressedBlockSize)
			{
				UE_LOG(LogPakFile, Verbose, TEXT("Bigger compressed? Block[%d]: %d -> %d > %d [%d min %d]"), CompressionBlockIndex, Block.CompressedStart, Block.CompressedEnd, UncompressedBlockSize, PakEntry.UncompressedSize - Pos, PakEntry.CompressionBlockSize);
			}


			int64 ReadSize = EncryptionPolicy::AlignReadRequest(CompressedBlockSize);
			int64 WriteSize = FMath::Min<int64>(UncompressedBlockSize - DirectCopyStart, Length);

			const bool bCurrentScratchTempBufferValid =
				bExistingScratchBufferValid && !bStartedUncompress
				// ensure this object was the last reader from the scratch buffer and the last thing it decompressed was this block.
				&& (ScratchSpace->LastPakEntryOffset == PakEntry.Offset)
				&& (ScratchSpace->LastPakIndexHash == PakFile.GetInfo().IndexHash)
				&& (ScratchSpace->LastDecompressedBlock == CompressionBlockIndex)
				// ensure the previous decompression destination was the scratch buffer.
				&& !(DirectCopyStart == 0 && Length >= CompressionBlockSize);

			if (bCurrentScratchTempBufferValid)
			{
				// Reuse the existing scratch buffer to avoid repeatedly deserializing and decompressing the same block.
				FMemory::Memcpy(V, ScratchSpace->TempBuffer.Get() + DirectCopyStart, WriteSize);
			}
			else
			{
				PakReader->Seek(Block.CompressedStart + (PakFile.GetInfo().HasRelativeCompressedChunkOffsets() ? PakEntry.Offset : 0));
				PakReader->Serialize(WorkingBuffers[CompressionBlockIndex & 1], ReadSize);
				if (bStartedUncompress)
				{
					UncompressTask.EnsureCompletion();
					bStartedUncompress = false;
				}

				FPakUncompressTask& TaskDetails = UncompressTask.GetTask();
				TaskDetails.EncryptionKeyGuid = PakFile.GetInfo().EncryptionKeyGuid;

				if (DirectCopyStart == 0 && Length >= CompressionBlockSize)
				{
					// Block can be decompressed directly into output buffer
					TaskDetails.CompressionFormat = CompressionMethod;
					TaskDetails.UncompressedBuffer = (uint8*)V;
					TaskDetails.UncompressedSize = UncompressedBlockSize;
					TaskDetails.CompressedBuffer = WorkingBuffers[CompressionBlockIndex & 1];
					TaskDetails.CompressedSize = CompressedBlockSize;
					TaskDetails.CopyOut = nullptr;
					ScratchSpace->LastDecompressedBlock = 0xFFFFFFFF;
					ScratchSpace->LastPakIndexHash = FSHAHash();
					ScratchSpace->LastPakEntryOffset = -1;
				}
				else
				{
					// Block needs to be copied from a working buffer
					TaskDetails.CompressionFormat = CompressionMethod;
					TaskDetails.UncompressedBuffer = ScratchSpace->TempBuffer.Get();
					TaskDetails.UncompressedSize = UncompressedBlockSize;
					TaskDetails.CompressedBuffer = WorkingBuffers[CompressionBlockIndex & 1];
					TaskDetails.CompressedSize = CompressedBlockSize;
					TaskDetails.CopyOut = V;
					TaskDetails.CopyOffset = DirectCopyStart;
					TaskDetails.CopyLength = WriteSize;
					ScratchSpace->LastDecompressedBlock = CompressionBlockIndex;
					ScratchSpace->LastPakIndexHash = PakFile.GetInfo().IndexHash;
					ScratchSpace->LastPakEntryOffset = PakEntry.Offset;
				}

				if (Length == WriteSize)
				{
					UncompressTask.StartSynchronousTask();
				}
				else
				{
					UncompressTask.StartBackgroundTask();
				}

				bStartedUncompress = true;
			}

			V = (void*)((uint8*)V + WriteSize);
			Length -= WriteSize;
			DirectCopyStart = 0;
			++CompressionBlockIndex;
		}

		if (bStartedUncompress)
		{
			UncompressTask.EnsureCompletion();
		}
	}
};

struct FPakUtils
{
	static IFileHandle* CreatePakFileHandle(IPlatformFile* LowerLevel, const TRefCountPtr<FPakFile>& PakFile, const FPakEntry* FileEntry)
	{
		IFileHandle* Result = nullptr;
		TAcquirePakReaderFunction AcquirePakReader = [StoredPakFile=TRefCountPtr<FPakFile>(PakFile), LowerLevelPlatformFile = LowerLevel]() -> FSharedPakReader
		{
			return StoredPakFile->GetSharedReader(LowerLevelPlatformFile);
		};

		// Create the handle.
		const TRefCountPtr<const FPakFile>& ConstPakFile = (const TRefCountPtr<const FPakFile>&)PakFile;
		if (FileEntry->CompressionMethodIndex != 0 && PakFile->GetInfo().Version >= FPakInfo::PakFile_Version_CompressionEncryption)
		{
			if (FileEntry->IsEncrypted())
			{
				Result = new FPakFileHandle<FPakCompressedReaderPolicy<FPakSimpleEncryption>>(ConstPakFile, *FileEntry, AcquirePakReader);
			}
			else
			{
				Result = new FPakFileHandle<FPakCompressedReaderPolicy<>>(ConstPakFile, *FileEntry, AcquirePakReader);
			}
		}
		else if (FileEntry->IsEncrypted())
		{
			Result = new FPakFileHandle<FPakReaderPolicy<FPakSimpleEncryption>>(ConstPakFile, *FileEntry, AcquirePakReader);
		}
		else
		{
			Result = new FPakFileHandle<>(ConstPakFile, *FileEntry, AcquirePakReader);
		}

		return Result;
	}
};
