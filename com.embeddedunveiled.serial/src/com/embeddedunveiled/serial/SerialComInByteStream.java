/**
 * Author : Rishi Gupta
 * 
 * This file is part of 'serial communication manager' library.
 *
 * The 'serial communication manager' is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by the Free Software 
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * The 'serial communication manager' is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with serial communication manager. If not, see <http://www.gnu.org/licenses/>.
 */
package com.embeddedunveiled.serial;

import java.io.IOException;
import java.io.InputStream;

/**
 * <p>This class represents an input stream of bytes which is received from serial port.</p>
 * 
 * <p>It implements blocking read as mentioned in standard Java. It is for this reason -1 
 * or EOF is never returned by any read method because it blocks until it reads data byte
 * or an exception occurs.</p>
<<<<<<< HEAD
=======
 * 
 * <p>Application design should make sure that the port is not closed if there exist a read method
 * which is blocked (waiting for data byte) on the same port.</p>
>>>>>>> upstream/master
 */
public final class SerialComInByteStream extends InputStream {

	private SerialComManager scm = null;
	private long handle = 0;
	private boolean isOpened = false;

	public SerialComInByteStream(SerialComManager scm, long handle) throws SerialComException {
		this.scm = scm;
		this.handle = handle;
		isOpened = true;
		
		// make read pure blocking in non-windows OS. For windows blocking read method is called.
		if(SerialComManager.getOSType() != SerialComManager.OS_WINDOWS) {
			scm.fineTuneRead(handle, 1, 0, 0, 0, 0);
		}
	}

	/**
	 * <p>Returns an estimate of the minimum number of bytes that can be read from this input stream
	 * without blocking by the next invocation of a method for this input stream.</p>
	 * 
	 * @return an estimate of the minimum number of bytes that can be read from this input stream without blocking
	 * @throws IOException if an I/O error occurs.
	 */
	@Override
	public int available() throws IOException {
		if(isOpened != true) {
			throw new IOException(SerialComErrorMapper.ERR_BYTE_STREAM_IS_CLOSED);
		}
		
		int[] numBytesAvailable = new int[2];
		try {
			numBytesAvailable = scm.getByteCountInPortIOBuffer(handle);
		} catch (SerialComException e) {
			throw new IOException(e);
		}
		return numBytesAvailable[0];
	}
	
	/**
	 * <p>This method releases the InputStream object associated with the operating handle.</p>
	 * <p>To actually close the port closeComPort() method should be used.</p>
	 */
	@Override
	public void close() throws IOException {
		if(isOpened != true) {
			throw new IOException(SerialComErrorMapper.ERR_BYTE_STREAM_IS_CLOSED);
		}
		scm.destroyInputByteStream(this);
		isOpened = false;
	}
	
	/**
	 * <p>scm does not support mark and reset of input stream. If required, it can be developed at application level.</p>
	 * 
	 */
	@Override
	public void mark(int a) {
	}

	/**
	 * <p>scm does not support mark and reset of input stream. If required, it can be developed at application level.</p>
	 * 
	 * @return always returns false
	 */
	@Override
	public boolean markSupported() {
		return false;
	}

	/**
	 * <p>Reads the next byte of data from the input stream. The value byte is returned as an int in the range 0 to 255.</p>
	 * 
	 * @return the next byte of data
	 * @throws IOException if an I/O error occurs or if input stream has been closed
	 */
	@Override
	public int read() throws IOException {
		if(isOpened != true) {
			throw new IOException(SerialComErrorMapper.ERR_BYTE_STREAM_IS_CLOSED);
		}
		
		byte[] data = new byte[1];
		try {
			data = scm.readBytesBlocking(handle, 1);
			if(data != null) {
				return (int)data[0];
			}
		}catch (SerialComException e) {
			throw new IOException(e);
		}

		return -1;
	}
	
    /**
     * <p>Reads some number of bytes from the input stream and stores them into the buffer array b. The number of bytes actually read is
     * returned as an integer.  This method blocks until input data is available or an exception is thrown.</p>
     *
     * <p>If the length of b is zero, then no bytes are read and 0 is returned; otherwise, there is an attempt to read at least one byte.</p>
     *
     * <p>The first byte read is stored into element b[0], the next one into b[1], and so on. The number of bytes read is, at most, equal to
     * the length of b. Let k be the number of bytes actually read; these bytes will be stored in elements b[0] through <code>b[</code><i>k</i><code>-1]</code>,
     * leaving elements <code>b[</code><i>k</i><code>]</code> through <code>b[b.length-1]</code> unaffected.</p>
     *
     * <p>The read(b) method for class SerialComInByteStream has the same effect as : read(b, 0, b.length) </p>
     *
     * @param  b the buffer into which the data is read.
     * @return the total number of bytes read into the buffer
     * @throws IOException if an I/O error occurs or if input stream has been closed
     * @throws NullPointerException  if <code>b</code> is <code>null</code>.
     */
	@Override
    public int read(byte b[]) throws IOException {
        return read(b, 0, b.length);
    }
	
    /**
     * <p>Reads up to len bytes of data from the input stream into an array of bytes. An attempt is made to read
     * as many as len bytes, but a smaller number may be read. The number of bytes actually read is returned as 
     * an integer.</p>
     * 
     * <p>This method blocks until input data is available or an exception is thrown.</p>
     * 
     * <p>If len is zero, then no bytes are read and 0 is returned; otherwise, there is an attempt to read at 
     * least one byte and stored into b.</p>
     * 
     * <p>The first byte read is stored into element b[off], the next one into b[off+1], and so on. The number 
     * of bytes read is, at most, equal to len. Let k be the number of bytes actually read; these bytes will be 
     * stored in elements b[off] through b[off+k-1], leaving elements b[off+k] through b[off+len-1] unaffected.</p>
     *  
     * <p>In every case, elements b[0] through b[off] and elements b[off+len] through b[b.length-1] are unaffected.</p>
     * 
     * @param b the buffer into which the data is read.
     * @param off the start offset in array b at which the data is written.
     * @param len the maximum number of bytes to read.
     * @return the total number of bytes read into the buffer or 0 if len is zero
     * @throws IOException if an I/O error occurs or if input stream has been closed
     * @throws NullPointerException if <code>b</code> is <code>null</code>.
     * @throws IllegalArgumentException if data is not a byte type array
     * @throws IndexOutOfBoundsException if off is negative, len is negative, or len is greater than b.length - off 
     */
	@Override
	public int read(byte b[], int off, int len) throws IOException {
		if(isOpened != true) {
			throw new IOException(SerialComErrorMapper.ERR_BYTE_STREAM_IS_CLOSED);
		}
		if(b == null) {
			throw new NullPointerException("read(), " + SerialComErrorMapper.ERR_READ_NULL_DATA_PASSED);
		}
		if((off < 0) || (len < 0) || (len > (b.length - off))) {
			throw new IndexOutOfBoundsException("read(), " + SerialComErrorMapper.ERR_INDEX_VIOLATION);
		}
		if(len == 0) {
			return 0;
		}
		if(!(b instanceof byte[])) {
			throw new IllegalArgumentException(SerialComErrorMapper.ERR_ARG_NOT_BYTE_ARRAY);
		}
		
		try {
			byte[] data = scm.readBytesBlocking(handle, len);
			if(data != null) {
				System.arraycopy(data, 0, b, off, data.length);
				return data.length;
			}
		}catch (SerialComException e) {
			throw new IOException(e);
		}
        
		return -1;
    }
	
	/**
	 * <p>The scm does not support reset. If required, it can be developed at application level.</p>
	 */
    public synchronized void reset() throws IOException {
    }

	/**
	 * <p>The scm does not support skip. If required, it can be developed at application level.</p>
	 * 
	 * @param number of bytes to skip
	 * @return always returns 0
	 */
	@Override
	public long skip(long number) {
		return 0;
	}
}