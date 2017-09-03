//
//  BarnFile.cpp
//  GEngine
//
//  Created by Clark Kromenaker on 8/4/17.
//

#include "BarnFile.h"
#include <iostream>
#include <fstream>
#include <vector>
#include "minilzo.h"
#include "zlib.h"

using namespace std;

BarnFile::BarnFile(const char* filePath) : mReader(filePath)
{
    // Make sure we can actually read this file.
    if(!mReader.CanRead())
    {
        cout << "Can't read barn file at " << filePath << endl;
        return;
    }
    
    // Two specific 4-byte ints must appear at the beginning of the file.
    // In text form, this is a string "GK3!Barn".
    unsigned int gameIdentifier = mReader.ReadUInt();
    unsigned int barnIdentifier = mReader.ReadUInt();
    if(gameIdentifier != kGameIdentifier && barnIdentifier != kBarnIdentifier)
    {
        cout << "Invalid file type!" << endl;
        return;
    }
    
    // 4-bytes: unknown constant value (65536)
    mReader.ReadUInt();
    
    // 4-bytes: unknown constant value (65536)
    mReader.ReadUInt();
    
    // 4-bytes: appears to be file size, or size of assets in BRN bundle.
    mReader.ReadUInt();
    
    // This value indicates the offset past the file header data to what I'd
    // call the "table of contents" or "toc".
    unsigned int tocOffset = mReader.ReadUInt();

    // This additional header data can be read in if desired, but it
    // isn't really relevant to the file functionality.
    /*
    {
        // 4-bytes: EXE/Content build # (119 in both cases)
        mReader.ReadUInt();
        mReader.ReadUInt();
        
        // 4-bytes: unknown value
        mReader.ReadUInt();
        
        // Two dates, 2-bytes per element.
        // The dates are both on the same day, just a few minutes apart.
        // Maybe like a build start/end time for the bundles?
        short year, month, day, hour, minute, second;
        year = mReader.ReadShort();
        month = mReader.ReadShort();
        mReader.ReadShort(); // unknown value
        day = mReader.ReadShort();
        hour = mReader.ReadShort();
        minute = mReader.ReadShort();
        second = mReader.ReadShort();
        cout << year << "/" << month << "/" << day << ", " << hour << ":" << minute << ":" << second << endl;
        
        // 2-bytes: unknown variable value.
        mReader.ReadShort();
        
        year = mReader.ReadShort();
        month = mReader.ReadShort();
        mReader.ReadShort(); // unknown value
        day = mReader.ReadShort();
        hour = mReader.ReadShort();
        minute = mReader.ReadShort();
        second = mReader.ReadShort();
        cout << year << "/" << month << "/" << day << ", " << hour << ":" << minute << ":" << second << endl;
        
        // 2-bytes: unknown variable value.
        mReader.ReadShort();
        
        // Copyright notice!
        char copyright[65];
        mReader.Read(copyright, 64);
        copyright[64] = '\0';
        cout << copyright << endl;
    }
    */
    
    // Seek to table of contents offset.
    mReader.Seek(tocOffset);
    
    // First value in toc is number of toc entries.
    unsigned int tocEntryCount = mReader.ReadUInt();
    
    // Each toc entry will specify a header offset and a data offset.
    vector<unsigned int> headerOffsets;
    headerOffsets.reserve(tocEntryCount);
    
    vector<unsigned int> dataOffsets;
    dataOffsets.reserve(tocEntryCount);
    
    // For each toc entry, read in relevant data.
    for(int i = 0; i < tocEntryCount; i++)
    {
        // The type is either "DDir" or "Data".
        // DDir specifies a directory of assets.
        // Data specifies file offset to start reading actual data.
        unsigned int type = mReader.ReadUInt();
        
        // Some unknown values.
        mReader.ReadUInt();
        mReader.ReadUInt();
        mReader.ReadUInt();
        mReader.ReadUInt();
        
        // Read header and data offsets.
        unsigned int headerOffset = mReader.ReadUInt();
        unsigned int dataOffset = mReader.ReadUInt();
        
        // For DDir, we'll save the offsets so we can iterate over them below.
        // For Data, we'll just save the data offset value.
        if(type == kDDirIdentifier)
        {
            headerOffsets.push_back(headerOffset);
            dataOffsets.push_back(dataOffset);
        }
        else if(type == kDataIdentifier)
        {
            mDataOffset = headerOffset;
        }
    }
    
    // Now we need to iterate over each header/data offset pair in turn.
    // The header specifies data that is common to all assets in the data section.
    for(int i = 0; i < headerOffsets.size(); i++)
    {
        mReader.Seek(headerOffsets[i]);
        
        // The name of the Barn file for these assets. NOTE that it appears
        // a Barn file can contain "pointers" to assets in other Barn files.
        // If this name is empty, it means the asset is contained within THIS Barn file.
        // However, if the name isn't empty, it means the asset is in another Barn file.
        char barnFileName[33];
        mReader.Read(barnFileName, 32);
        barnFileName[32] = '\0';
        
        // Unknown value.
        mReader.ReadUInt();
        
        // A human-readable description for this Barn file.
        // Ex: "Gabriel Knight 3 Day 1/2/3 Common"
        char barnDescription[40];
        mReader.Read(barnDescription, 40);
        
        // Unknown value.
        mReader.ReadUInt();
        
        int numAssets = mReader.ReadUInt();
        
        mReader.Seek(dataOffsets[i]);
        
        for(int j = 0; j < numAssets; j++)
        {
            BarnAsset asset;
            
            // The asset should save which Barn file it is in.
            // This will help later when trying to load assets.
            asset.barnFileName = barnFileName;
            
            // Asset size, in bytes, but we need to read compression
            // value before we know whether this is compressed or uncompressed size.
            unsigned int assetSize = mReader.ReadUInt();
            
            // Read in the asset offset. This is the offset from the start of the data section.
            asset.offset = mReader.ReadUInt();
            
            // Unknown values.
            mReader.ReadUInt();
            mReader.ReadUByte();
            
            // Read in compression type.
            asset.compressionType = (CompressionType)mReader.ReadUByte();
            
            // Compression type 3 should just be treated as type none.
            // Not sure if type 3 is actually different in some way?
            if((int)asset.compressionType == 3)
            {
                asset.compressionType = CompressionType::None;
            }
            
            // If no compression, uncompressed/compressed sizes are the same.
            // If compressed, we only know the compressed size for now.
            if(asset.compressionType == CompressionType::None)
            {
                asset.uncompressedSize = asset.compressedSize = assetSize;
            }
            else
            {
                asset.compressedSize = assetSize;
                
                // If the barn file name is empty, it means the asset is in THIS file.
                // So, we can actually seek to that offset in the file and read the uncompressed size.
                if(asset.barnFileName.empty())
                {
                    int pos = mReader.GetPosition();
                    mReader.Seek(mDataOffset + asset.offset);
                    asset.uncompressedSize = mReader.ReadUInt();
                    mReader.Seek(pos);
                }
            }
            
            // Read in asset name. This name appears to be null-terminated (+1).
            // So, max size is 256 + 1 = 257.
            //TODO: Might be better to only store a char array of the correct length?
            unsigned int assetNameLength = mReader.ReadUByte();
            char assetName[257];
            mReader.Read(assetName, assetNameLength + 1);
            
            // Save asset name.
            asset.name = assetName;
            
            // Map asset name to asset for fast lookup later.
            mAssetMap[asset.name] = asset;
        }
    }
    
    OutputAssetList();
}

bool BarnFile::CanRead() const
{
    return mReader.CanRead();
}

void BarnFile::OutputAssetList()
{
    unordered_map<string, BarnAsset>::iterator it;
    for(it = mAssetMap.begin(); it != mAssetMap.end(); it++)
    {
        if(!it->second.barnFileName.empty()) { continue; }
        
        cout << it->second.name << " - " << (int)it->second.compressionType;
        
        cout << " - " << it->second.compressedSize;
        if(it->second.compressionType != CompressionType::None)
        {
            cout << " - " << it->second.uncompressedSize;
        }
    
        if(!it->second.barnFileName.empty())
        {
            cout << " (" << it->second.barnFileName << ")";
        }
        cout << endl;
    }
}

BarnAsset* BarnFile::GetAsset(const std::string assetName)
{
    unordered_map<string, BarnAsset>::iterator it = mAssetMap.find(assetName);
    if(it != mAssetMap.end())
    {
        return &it->second;
    }
    return nullptr;
}

bool BarnFile::Extract(const std::string assetName, char *buffer, int bufferSize)
{
    // Get the asset handle associated with this asset name.
    // We fail if we can't find the asset with that name.
    BarnAsset* asset = GetAsset(assetName);
    if(asset == nullptr)
    {
        cout << "No asset named " << assetName << "in Barn file!" << endl;
        return false;
    }
    
    // Make sure this asset actually exists within this barn file, and it isn't a pointer to another barn file.
    if(!asset->barnFileName.empty())
    {
        cout << "Asset " << assetName << " can't be extracted from Barn - it is only an asset pointer!" << endl;
        return false;
    }
    
    // If the buffer provided is too small for the asset, we can't extract it. Ideally, the buffer is EXACTLY the right size!
    if(bufferSize < asset->uncompressedSize)
    {
        cout << "Buffer is too small to cotain extracted asset." << endl;
        return false;
    }
    
    // Method used to extract will depend upon the compression type for the asset.
    if(asset->compressionType == CompressionType::None)
    {
        // Seek to the data possion and read the data into the buffer. Since it's already uncompressed, we're done!
        cout << "Reading from offset " << mDataOffset + asset->offset << endl;
        cout << "Reading " << asset->uncompressedSize << " bytes " << endl;
        mReader.Seek(mDataOffset + asset->offset);
        mReader.Read(buffer, asset->uncompressedSize);
    }
    else if(asset->compressionType == CompressionType::Zlib)
    {
        // Read compressed data into a buffer.
        unsigned char* compressedBuffer = new unsigned char[asset->compressedSize];
        mReader.Seek(mDataOffset + 8 + asset->offset);
        mReader.Read(compressedBuffer, asset->compressedSize);
    
        z_stream strm;
        strm.next_in = compressedBuffer;
        strm.avail_in = asset->compressedSize;
        strm.next_out = (unsigned char*)buffer;
        strm.avail_out = bufferSize;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        
        // Make sure zlib is initialized for "inflation".
        int result = inflateInit(&strm);
        if(result != Z_OK)
        {
            cout << "Error when calling inflateInit: " << result << endl;
            delete[] compressedBuffer;
            return false;
        }
        
        // Inflate the data!
        result = inflate(&strm, Z_FINISH);
        if(result != Z_STREAM_END)
        {
            cout << "Inflate didn't inflate entire stream, or an error occurred: " << result << endl;
            delete[] compressedBuffer;
            return false;
        }
        
        // Uninit zlib.
        result = inflateEnd(&strm);
        if(result != Z_OK)
        {
            cout << "Error while ending inflate: " << result << endl;
            delete[] compressedBuffer;
            return false;
        }

        // Delete compressed data buffer.
        delete[] compressedBuffer;
    }
    else if(asset->compressionType == CompressionType::Lzo)
    {
        // Read compressed data into a buffer.
        char* compressedBuffer = new char[asset->compressedSize];
        mReader.Seek(mDataOffset + 8 + asset->offset);
        mReader.Read(compressedBuffer, asset->compressedSize);
        
        // Make sure LZO library is initialized.
        lzo_init();
        
        // Decompress using LZO library
        lzo1x_decompress((lzo_bytep)compressedBuffer, (lzo_uint)asset->compressedSize, (lzo_bytep)buffer, (lzo_uintp)&bufferSize, nullptr);
        
        // Delete compressed data buffer.
        delete[] compressedBuffer;
    }
    else
    {
        cout << "Asset " << assetName << " has invalid compression type " << (int)asset->compressionType << endl;
        return false;
    }
    
    // Success!
    return true;
}

bool BarnFile::WriteToFile(const std::string assetName)
{
    BarnAsset* asset = GetAsset(assetName);
    if(asset == nullptr)
    {
        cout << "No asset named " << assetName << "in Barn file!" << endl;
        return false;
    }
    
    if(!asset->barnFileName.empty())
    {
        cout << "Asset " << assetName << " can't be extracted from Barn - it is only an asset pointer!" << endl;
        return false;
    }
    
    bool result = false;
    char* assetData = new char[asset->uncompressedSize];
    if(Extract(assetName, assetData, asset->uncompressedSize))
    {
        ofstream fileStream(asset->name);
        if(fileStream.good())
        {
            fileStream.write(assetData, asset->uncompressedSize);
            fileStream.close();
            cout << "Wrote out " << asset->name << endl;
            result = true;
        }
    }
    else
    {
        cout << "Error while extracting uncompressed asset." << endl;
    }
    delete[] assetData;
    return result;
}
