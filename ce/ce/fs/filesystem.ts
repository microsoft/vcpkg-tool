// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/* eslint-disable @typescript-eslint/ban-types */

import { EventEmitter } from 'ee-ts';
import { Readable, Writable } from 'stream';
import { Session } from '../session';
import { Uri } from '../util/uri';

const size64K = 1 << 16;
const size32K = 1 << 15;

/**
 * The `FileStat`-type represents metadata about a file
 */
export interface FileStat {
  /**
   * The type of the file, e.g. is a regular file, a directory, or symbolic link
   * to a file.
   *
   * *Note:* This value might be a bitmask, e.g. `FileType.File | FileType.SymbolicLink`.
   */
  type: FileType;
  /**
   * The creation timestamp in milliseconds elapsed since January 1, 1970 00:00:00 UTC.
   */
  ctime: number;
  /**
   * The modification timestamp in milliseconds elapsed since January 1, 1970 00:00:00 UTC.
   *
   * *Note:* If the file changed, it is important to provide an updated `mtime` that advanced
   * from the previous value. Otherwise there may be optimizations in place that will not show
   * the updated file contents in an editor for example.
   */
  mtime: number;
  /**
   * The size in bytes.
   *
   * *Note:* If the file changed, it is important to provide an updated `size`. Otherwise there
   * may be optimizations in place that will not show the updated file contents in an editor for
   * example.
   */
  size: number;
  /**
   * The file mode (unix permissions).
   */
  mode: number;
}

/**
* Enumeration of file types. The types `File` and `Directory` can also be
* a symbolic links, in that case use `FileType.File | FileType.SymbolicLink` and
* `FileType.Directory | FileType.SymbolicLink`.
*/
export enum FileType {
  /**
   * The file type is unknown.
   */
  Unknown = 0,
  /**
   * A regular file.
   */
  File = 1,
  /**
   * A directory.
   */
  Directory = 2,
  /**
   * A symbolic link to a file.
   */
  SymbolicLink = 64
}

export interface WriteStreamOptions {
  append?: boolean;
  mode?: number;
  mtime?: Date;
}

/**
 * A random-access reading interface to access a file in a FileSystem.
 *
 * Ideally, we keep reads in a file to a forward order, so that this can be implemented on filesystems
 * that do not support random access (ie, please do your best to order reads so that they go forward only as much as possible)
 *
 * Underneath on FSes that do not support random access, this would likely require multiple 'open' operation for the same
 * target file.
 */
export abstract class ReadHandle {
  /**
   * Reads a block from a file
   *
   * @param buffer The buffer that the data will be written to.
   * @param offset The offset in the buffer at which to start writing.
   * @param length The number of bytes to read.
   * @param position The offset from the beginning of the file from which data should be read. If `null`, data will be read from the current position.
   */
  abstract read<TBuffer extends Uint8Array>(buffer: TBuffer, offset?: number | null, length?: number | null, position?: number | null): Promise<{ bytesRead: number, buffer: TBuffer }>;

  async readComplete<TBuffer extends Uint8Array>(buffr: TBuffer, offset = 0, length = buffr.byteLength, position: number | null = null, totalRead = 0): Promise<{ bytesRead: number, buffer: TBuffer }> {
    const { bytesRead, buffer } = await this.read(buffr, offset, length, position);
    if (length) {
      if (bytesRead && bytesRead < length) {
        return await this.readComplete(buffr, offset + bytesRead, length - bytesRead, position ? position + bytesRead : null, bytesRead + totalRead);
      }
    }
    return { bytesRead: bytesRead + totalRead, buffer };
  }
  /**
   * Returns a Readable for consuming an opened ReadHandle
   * @param start the first byte to read of the target
   * @param end the last byte to read of the target (inclusive!)
   */
  readStream(start = 0, end = Infinity): Readable {
    return Readable.from(asyncIterableOverHandle(start, end, this), {});
  }

  abstract size(): Promise<number>;

  abstract close(): Promise<void>;

  range(start: number, length: number) {
    return new RangeReadHandle(this, start, length);
  }
}

class RangeReadHandle extends ReadHandle {

  pos = 0;
  readHandle?: ReadHandle;

  constructor(readHandle: ReadHandle, private start: number, private length: number) {
    super();
    this.readHandle = readHandle;
  }

  async read<TBuffer extends Uint8Array>(buffer: TBuffer, offset?: number | null, length?: number | null, position?: number | null): Promise<{ bytesRead: number; buffer: TBuffer; }> {
    if (this.readHandle) {
      position = position !== undefined && position !== null ? (position + this.start) : (this.pos + this.start);
      length = length === null ? this.length : length;

      const result = await this.readHandle.read(buffer, offset, length, position);
      this.pos += result.bytesRead;
      return result;
    }

    return {
      bytesRead: 0, buffer
    };

  }

  async size(): Promise<number> {
    return this.length;
  }

  async close(): Promise<void> {
    this.readHandle = undefined;
  }

}

/**
 * Picks a reasonable buffer size. Not more than 64k
 *
 * @param length
 */
function reasonableBuffer(length: number) {
  return Buffer.alloc(length > size64K ? size32K : length);
}

/**
 * Creates an AsyncIterable<Buffer> over a ReadHandle
 * @param start the first byte in the target read from
 * @param end the last byte in the target to read from
 * @param handle the ReadHandle
 */
async function* asyncIterableOverHandle(start: number, end: number, handle: ReadHandle): AsyncIterable<Buffer> {
  while (start < end) {
    // buffer alloc must be inside the loop; zlib will hold the buffers until it can deal with a whole stream.
    const buffer = reasonableBuffer(1 + end - start);
    const count = Math.min(1 + end - start, buffer.byteLength);
    const b = await handle.read(buffer, 0, count, start);
    if (b.bytesRead === 0) {
      return;
    }
    start += b.bytesRead;
    // return only what was actually read. (just a view)
    if (b.bytesRead === buffer.byteLength) {
      yield buffer;
    }
    else {
      yield buffer.slice(0, b.bytesRead);
    }
  }
}

export abstract class FileSystem extends EventEmitter<FileSystemEvents> {

  protected baseUri?: Uri;

  /**
 * Creates a new URI from a file system path, e.g. `c:\my\files`,
 * `/usr/home`, or `\\server\share\some\path`.
 *
 * associates this FileSystem with the Uri
 *
 * @param path A file system path (see `URI#fsPath`)
 */
  file(path: string): Uri {
    return Uri.file(this, path);
  }

  /** construct an Uri from the various parts */
  from(components: {
    scheme: string;
    authority?: string;
    path?: string;
    query?: string;
    fragment?: string;
  }): Uri {
    return Uri.from(this, components);
  }

  /**
 * Creates a new URI from a string, e.g. `https://www.msft.com/some/path`,
 * `file:///usr/home`, or `scheme:with/path`.
 *
 * @param value A string which represents an URI (see `URI#toString`).
 */
  parse(value: string, _strict?: boolean): Uri {
    return Uri.parse(this, value, _strict);
  }

  /**
   * Retrieve metadata about a file.
   *
   * @param uri The uri of the file to retrieve metadata about.
   * @return The file metadata about the file.
   */
  abstract stat(uri: Uri, options?: {}): Promise<FileStat>;

  /**
   * Retrieve all entries of a [directory](#FileType.Directory).
   *
   * @param uri The uri of the folder.
   * @return An array of name/type-tuples or a Promise that resolves to such.
   */
  abstract readDirectory(uri: Uri, options?: { recursive?: boolean }): Promise<Array<[Uri, FileType]>>;

  /**
   * Create a new directory (Note, that new files are created via `write`-calls).
   *
   * *Note* that missing directories are created automatically, e.g this call has
   * `mkdirp` semantics.
   *
   * @param uri The uri of the new folder.
   */
  abstract createDirectory(uri: Uri, options?: {}): Promise<void>;

  /**
   * Read the entire contents of a file.
   *
   * @param uri The uri of the file.
   * @return An array of bytes or a Promise that resolves to such.
   */
  abstract readFile(uri: Uri, options?: {}): Promise<Uint8Array>;

  /**
   * Creates a stream to read a file from the filesystem
   *
   * @param uri The uri of the file.
   * @return a Readable stream
   */
  abstract readStream(uri: Uri, options?: { start?: number, end?: number }): Promise<Readable>;

  /**
   * Write data to a file, replacing its entire contents.
   *
   * @param uri The uri of the file.
   * @param content The new content of the file.
   */
  abstract writeFile(uri: Uri, content: Uint8Array): Promise<void>;

  /**
   * Creates a stream to write a file to the filesystem
   *
   * @param uri The uri of the file.
   * @return a Writeable stream
   */
  abstract writeStream(uri: Uri, options?: WriteStreamOptions): Promise<Writable>;

  /**
   * Delete a file.
   *
   * @param uri The resource that is to be deleted.
   * @param options Defines if trash can should be used and if deletion of folders is recursive
   */
  abstract delete(uri: Uri, options?: { recursive?: boolean, useTrash?: boolean }): Promise<void>;

  /**
   * Rename a file or folder.
   *
   * @param oldUri The existing file.
   * @param newUri The new location.
   * @param options Defines if existing files should be overwritten.
   */
  abstract rename(source: Uri, target: Uri, options?: { overwrite?: boolean }): Promise<void>;

  abstract openFile(uri: Uri): Promise<ReadHandle>;

  /**
   * Copy files or folders.
   *
   * @param source The existing file.
   * @param destination The destination location.
   * @param options Defines if existing files should be overwritten.
   */
  abstract copy(source: Uri, target: Uri, options?: { overwrite?: boolean }): Promise<number>;

  abstract createSymlink(symlink: Uri, target: Uri): Promise<void>;

  /** checks to see if the target exists */
  async exists(uri: Uri) {
    try {
      return !!(await this.stat(uri));
    } catch (e) {
      // if this fails, we're assuming false
    }
    return false;
  }

  /** checks to see if the target is a directory/folder */
  async isDirectory(uri: Uri) {
    try {
      return !!((await this.stat(uri)).type & FileType.Directory);
    } catch {
      // if this fails, we're assuming false
    }
    return false;
  }

  /** checks to see if the target is a file */
  async isFile(uri: Uri) {
    try {
      const s = await this.stat(uri);

      return !!(s.type & FileType.File);
    } catch {
      // if this fails, we're assuming false
    }
    return false;
  }

  /** checks to see if the target is a symbolic link */
  async isSymlink(uri: Uri) {
    try {
      return !!((await this.stat(uri)) && FileType.SymbolicLink);
    } catch {
      // if this fails, we're assuming false
    }
    return false;
  }

  constructor(protected session: Session) {
    super();
  }

  /** EventEmitter for when files are read */
  protected read(path: Uri, context?: any) {
    this.emit('read', path, context, this.session.stopwatch.total);
  }

  /** EventEmitter for when files are written */
  protected write(path: Uri, context?: any) {
    this.emit('write', path, context, this.session.stopwatch.total);
  }

  /** EventEmitter for when files are deleted */
  protected deleted(path: Uri, context?: any) {
    this.emit('deleted', path, context, this.session.stopwatch.total);
  }

  /** EventEmitter for when files are renamed */
  protected renamed(path: Uri, context?: any) {
    this.emit('renamed', path, context, this.session.stopwatch.total);
  }

  /** EventEmitter for when directories are read */
  protected directoryRead(path: Uri, contents?: Promise<Array<[Uri, FileType]>>) {
    this.emit('directoryRead', path, contents, this.session.stopwatch.total);
  }

  /** EventEmitter for when direcotries are created */
  protected directoryCreated(path: Uri, context?: any) {
    this.emit('directoryCreated', path, context, this.session.stopwatch.total);
  }
}


/** Event definitions for FileSystem events */
interface FileSystemEvents {
  read(path: Uri, context: any, msec: number): void;
  write(path: Uri, context: any, msec: number): void;
  deleted(path: Uri, context: any, msec: number): void;
  renamed(path: Uri, context: any, msec: number): void;
  directoryRead(path: Uri, contents: Promise<Array<[Uri, FileType]>> | undefined, msec: number): void;
  directoryCreated(path: Uri, context: any, msec: number): void;
}
