#pragma once

#include "FModel.h"
#include "Serialization/Archive.h"
#include "Compression/lz4.h"
#include "Serialization/MemoryReader.h"

#define IS_LZ4 0x184D2204

class FFModelBackupResource
{
public:
	FFModelBackupResource(FArchive& InAr)
	{
		uint32 Magic;
		InAr << Magic;
		FArchive* ArToUse;
		if (Magic == IS_LZ4)
		{
			TArray<uint8> Compressed;
			Compressed.SetNumUninitialized(InAr.TotalSize());
			InAr.Seek(0);
			InAr.Serialize(Compressed.GetData(), InAr.TotalSize());
			TArray<uint8> Uncompressed;
			LZ4_streamDecode_t* Stream = LZ4_createStreamDecode();
			uint64 TotalDecompressed = 0;
			while (true)
			{
				Uncompressed.SetNumUninitialized(Uncompressed.Num() + 8192);
				int32 DecompressedSize = LZ4_decompress_safe_continue(Stream, reinterpret_cast<const char*>(Compressed.GetData()), reinterpret_cast<char*>(Uncompressed.GetData() + TotalDecompressed), Compressed.Num(), 8192);
				check(DecompressedSize >= 0);
				if (DecompressedSize == 0)
				{
					break;
				}
				TotalDecompressed += DecompressedSize;
			}
			Uncompressed.SetNumUninitialized(TotalDecompressed);
			LZ4_freeStreamDecode(Stream);
			ArToUse = new FMemoryReader(Uncompressed);
		}
		else
		{
			ArToUse = &InAr;
		}

		FArchive& Ar = *ArToUse;
		while (!Ar.AtEnd())
		{

		}

		delete ArToUse;
	}
};
