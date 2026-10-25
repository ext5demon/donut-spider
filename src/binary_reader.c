#include "binary_reader.h"
#include "binary_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

BinaryReader BinaryReader_create(FILE* file, size_t fileSize) {
    BinaryReader br = {0};
    br.file = file;
    br.fileSize = fileSize;
    return br;
}

void BinaryReader_setFileCache(BinaryReader* reader, uint8_t* cache, size_t capacity) {
    require(cache != nullptr && capacity > 0 && reader->buffer == nullptr);
    reader->fileCachePosition = BinaryReader_getPosition(reader);
    reader->fileCache = cache;
    reader->fileCacheCapacity = capacity;
    reader->fileCacheLength = 0;
    reader->fileCacheFills = reader->fileCacheBytesRead = 0;
}

void BinaryReader_setBuffer(BinaryReader* reader, uint8_t* buffer, size_t baseOffset, size_t size) {
    reader->buffer = buffer;
    reader->bufferBase = baseOffset;
    reader->bufferSize = size;
    reader->bufferPos = 0;
}

void BinaryReader_clearBuffer(BinaryReader* reader) {
    reader->buffer = nullptr;
    reader->bufferBase = 0;
    reader->bufferSize = 0;
    reader->bufferPos = 0;
}

static void readCheck(BinaryReader* reader, void* dest, size_t bytes) {
    if (reader->buffer != nullptr) {
        if (reader->bufferPos + bytes > reader->bufferSize) {
            size_t absPos = reader->bufferBase + reader->bufferPos;
            fprintf(stderr, "BinaryReader: buffer read error at position 0x%zX (requested %zu bytes, buffer has %zu remaining)\n", absPos, bytes, reader->bufferSize - reader->bufferPos);
            abort();
        }
        memcpy(dest, reader->buffer + reader->bufferPos, bytes);
        reader->bufferPos += bytes;
        return;
    }

    if (reader->fileCache != nullptr) {
        require(reader->fileCachePosition <= reader->fileSize && bytes <= reader->fileSize-reader->fileCachePosition);
        uint8_t* output = dest;
        while (bytes > 0) {
            size_t position = reader->fileCachePosition;
            if (position < reader->fileCacheBase || position-reader->fileCacheBase >= reader->fileCacheLength) {
                size_t count = reader->fileSize-position;
                if (count > reader->fileCacheCapacity) count = reader->fileCacheCapacity;
                require(fseek(reader->file, (long)position, SEEK_SET) == 0);
                require(fread(reader->fileCache, 1, count, reader->file) == count);
                reader->fileCacheBase = position;
                reader->fileCacheLength = count;
                ++reader->fileCacheFills;
                reader->fileCacheBytesRead += count;
            }
            size_t offset = position-reader->fileCacheBase;
            size_t count = reader->fileCacheLength-offset;
            if (count > bytes) count = bytes;
            memcpy(output, reader->fileCache+offset, count);
            output += count; bytes -= count; reader->fileCachePosition += count;
        }
        return;
    }
    size_t read = fread(dest, 1, bytes, reader->file);
    if (read != bytes) {
        long pos = ftell(reader->file) - (long) read;
        fprintf(stderr, "BinaryReader: read error at position 0x%lX (requested %zu bytes, got %zu, file size 0x%zX)\n", pos, bytes, read, reader->fileSize);
        abort();
    }
}

uint8_t BinaryReader_readUint8(BinaryReader* reader) {
    uint8_t value;
    readCheck(reader, &value, 1);
    return value;
}

int16_t BinaryReader_readInt16(BinaryReader* reader) {
    uint16_t value;
    readCheck(reader, &value, sizeof(value));
    return (int16_t) BinaryUtils_toLittle16(value);
}

uint16_t BinaryReader_readUint16(BinaryReader* reader) {
    uint16_t value;
    readCheck(reader, &value, sizeof(value));
    return BinaryUtils_toLittle16(value);
}

int32_t BinaryReader_readInt32(BinaryReader* reader) {
    uint32_t value;
    readCheck(reader, &value, sizeof(value));
    return (int32_t) BinaryUtils_toLittle32(value);
}

uint32_t BinaryReader_readUint32(BinaryReader* reader) {
    uint32_t value;
    readCheck(reader, &value, sizeof(value));
    return BinaryUtils_toLittle32(value);
}

float BinaryReader_readFloat32(BinaryReader* reader) {
    uint32_t bits;
    float value;
    readCheck(reader, &bits, sizeof(bits));
    bits = BinaryUtils_toLittle32(bits);
    memcpy(&value, &bits, sizeof(value));
    return value;
}

uint64_t BinaryReader_readUint64(BinaryReader* reader) {
    uint64_t value;
    readCheck(reader, &value, sizeof(value));
    return BinaryUtils_toLittle64(value);
}

int64_t BinaryReader_readInt64(BinaryReader* reader) {
    uint64_t value;
    readCheck(reader, &value, sizeof(value));
    return (int64_t) BinaryUtils_toLittle64(value);
}

bool BinaryReader_readBool32(BinaryReader* reader) {
    return BinaryReader_readUint32(reader) != 0;
}

void BinaryReader_readBytes(BinaryReader* reader, void* dest, size_t count) {
    readCheck(reader, dest, count);
}

uint8_t* BinaryReader_readBytesAt(BinaryReader* reader, size_t offset, size_t count) {
    uint8_t* buf = (uint8_t *)safeMalloc(count);

    if (reader->buffer != nullptr) {
        if (offset < reader->bufferBase || offset + count > reader->bufferBase + reader->bufferSize) {
            fprintf(stderr, "BinaryReader: readBytesAt offset 0x%zX+%zu out of buffer range [0x%zX, 0x%zX)\n", offset, count, reader->bufferBase, reader->bufferBase + reader->bufferSize);
            abort();
        }
        size_t savedPos = reader->bufferPos;
        memcpy(buf, reader->buffer + (offset - reader->bufferBase), count);
        reader->bufferPos = savedPos;
        return buf;
    }

    if (reader->fileCache != nullptr) {
        size_t savedPos = reader->fileCachePosition;
        BinaryReader_seek(reader, offset);
        readCheck(reader, buf, count);
        reader->fileCachePosition = savedPos;
        return buf;
    }
    long savedPos = ftell(reader->file);
    fseek(reader->file, (long) offset, SEEK_SET);
    readCheck(reader, buf, count);
    fseek(reader->file, savedPos, SEEK_SET);
    return buf;
}

void BinaryReader_skip(BinaryReader* reader, size_t bytes) {
    if (reader->buffer != nullptr) {
        reader->bufferPos += bytes;
        return;
    }
    if (reader->fileCache != nullptr) {
        require(reader->fileCachePosition <= reader->fileSize && bytes <= reader->fileSize-reader->fileCachePosition);
        reader->fileCachePosition += bytes;
        return;
    }
    fseek(reader->file, (long) bytes, SEEK_CUR);
}

void BinaryReader_seek(BinaryReader* reader, size_t position) {
    if (reader->buffer != nullptr) {
        if (position < reader->bufferBase || position > reader->bufferBase + reader->bufferSize) {
            fprintf(stderr, "BinaryReader: buffer seek to 0x%zX out of buffer range [0x%zX, 0x%zX]\n", position, reader->bufferBase, reader->bufferBase + reader->bufferSize);
            abort();
        }
        reader->bufferPos = position - reader->bufferBase;
        return;
    }

    if (position > reader->fileSize) {
        fprintf(stderr, "BinaryReader: seek to 0x%zX out of bounds (file size 0x%zX)\n", position, reader->fileSize);
        abort();
    }
    if (reader->fileCache != nullptr) {
        reader->fileCachePosition = position;
        return;
    }
    fseek(reader->file, (long) position, SEEK_SET);
}

size_t BinaryReader_getPosition(BinaryReader* reader) {
    if (reader->buffer != nullptr) {
        return reader->bufferBase + reader->bufferPos;
    }
    if (reader->fileCache != nullptr) return reader->fileCachePosition;
    return (size_t) ftell(reader->file);
}
