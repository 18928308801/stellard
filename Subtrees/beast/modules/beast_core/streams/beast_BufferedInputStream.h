//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_BUFFEREDINPUTSTREAM_BEASTHEADER
#define BEAST_BUFFEREDINPUTSTREAM_BEASTHEADER

#include "beast_InputStream.h"
#include "../memory/beast_OptionalScopedPointer.h"
#include "../memory/beast_HeapBlock.h"


//==============================================================================
/** Wraps another input stream, and reads from it using an intermediate buffer

    If you're using an input stream such as a file input stream, and making lots of
    small read accesses to it, it's probably sensible to wrap it in one of these,
    so that the source stream gets accessed in larger chunk sizes, meaning less
    work for the underlying stream.
*/
class BEAST_API BufferedInputStream  : public InputStream
{
public:
    //==============================================================================
    /** Creates a BufferedInputStream from an input source.

        @param sourceStream                 the source stream to read from
        @param bufferSize                   the size of reservoir to use to buffer the source
        @param deleteSourceWhenDestroyed    whether the sourceStream that is passed in should be
                                            deleted by this object when it is itself deleted.
    */
    BufferedInputStream (InputStream* sourceStream,
                         int bufferSize,
                         bool deleteSourceWhenDestroyed);

    /** Creates a BufferedInputStream from an input source.

        @param sourceStream     the source stream to read from - the source stream  must not
                                be deleted until this object has been destroyed.
        @param bufferSize       the size of reservoir to use to buffer the source
    */
    BufferedInputStream (InputStream& sourceStream, int bufferSize);

    /** Destructor.

        This may also delete the source stream, if that option was chosen when the
        buffered stream was created.
    */
    ~BufferedInputStream();


    //==============================================================================
    int64 getTotalLength();
    int64 getPosition();
    bool setPosition (int64 newPosition);
    int read (void* destBuffer, int maxBytesToRead);
    String readString();
    bool isExhausted();


private:
    //==============================================================================
    OptionalScopedPointer<InputStream> source;
    int bufferSize;
    int64 position, lastReadPos, bufferStart, bufferOverlap;
    HeapBlock <char> buffer;
    void ensureBuffered();

    BEAST_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BufferedInputStream)
};

#endif   // BEAST_BUFFEREDINPUTSTREAM_BEASTHEADER