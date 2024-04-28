// Copyright (C) 2004-2024 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHUNKREADER_H
#define CHUNKREADER_H

#include <QDataStream>
#include <QStack>

QT_FORWARD_DECLARE_CLASS(QIODevice)


template<std::size_t N>
constexpr quint32 ChunkId(char const(&str)[N])
{
    static_assert(N-1 == 4, "Chunk ID must be 4 characters long");
    return (quint32(str[3] & 0x7f) << 24) | (quint32(str[2] & 0x7f) << 16) |
           (quint32(str[1] & 0x7f) << 8) | quint32(str[0] & 0x7f);
}

constexpr quint64 ChunkVersion(uint v)
{
    return quint64(v) << 32;
}


class ChunkReader {
public:
    ChunkReader(QIODevice *dev, QDataStream::ByteOrder bo);

    QDataStream &dataStream();

    bool startChunk();
    bool endChunk();
    bool skipChunk();

    quint32 chunkId() const;
    quint32 chunkVersion() const;
    qint64 chunkSize() const;

private:
    struct read_chunk_info {
        quint32 id;
        quint32 version;
        qint64 startpos;
        qint64 size;
    };

    QStack<read_chunk_info> m_chunks;
    QIODevice * m_file;
    QDataStream m_stream;
};

class ChunkWriter {
public:
    ChunkWriter(QIODevice *dev, QDataStream::ByteOrder bo);
    ~ChunkWriter();

    QDataStream &dataStream();

    bool startChunk(quint32 id, quint32 version = 0);
    bool endChunk();

private:
    struct write_chunk_info {
        quint32 id;
        quint32 version;
        qint64 startpos;
    };

    QStack<write_chunk_info> m_chunks;
    QIODevice * m_file;
    QDataStream m_stream;
};

#endif
