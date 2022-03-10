// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Readable, Writable } from 'stream';
import { Uri } from '../util/uri';
import { FileStat, FileSystem, FileType, ReadHandle } from './filesystem';
import { get, getStream, head } from './https';

/**
 * HTTPS Filesystem
 *
 */
export class HttpsFileSystem extends FileSystem {

  async stat(uri: Uri): Promise<FileStat> {
    const result = await head(uri);

    return {
      type: FileType.File,
      mtime: Date.parse(result.headers.date || ''),
      ctime: Date.parse(result.headers.date || ''),
      size: Number.parseInt(result.headers['content-length'] || '0'),
      mode: 0o555 // https is read only but always 'executable'
    };
  }
  readDirectory(uri: Uri): Promise<Array<[Uri, FileType]>> {
    throw new Error('Method not implemented');
  }
  createDirectory(uri: Uri): Promise<void> {
    throw new Error('Method not implemented');
  }
  async readFile(uri: Uri): Promise<Uint8Array> {
    return (await get(uri)).rawBody;
  }
  writeFile(uri: Uri, content: Uint8Array): Promise<void> {
    throw new Error('Method not implemented');
  }
  delete(uri: Uri, options?: { recursive?: boolean | undefined; useTrash?: boolean | undefined; }): Promise<void> {
    throw new Error('Method not implemented');
  }
  rename(source: Uri, target: Uri, options?: { overwrite?: boolean | undefined; }): Promise<void> {
    throw new Error('Method not implemented');
  }
  copy(source: Uri, target: Uri, options?: { overwrite?: boolean | undefined; }): Promise<number> {
    throw new Error('Method not implemented');
  }
  async createSymlink(original: Uri, symlink: Uri): Promise<void> {
    throw new Error('Method not implemented');
  }
  async readStream(uri: Uri, options?: { start?: number, end?: number }): Promise<Readable> {
    return getStream(uri, options);
  }
  writeStream(uri: Uri): Promise<Writable> {
    throw new Error('Method not implemented');
  }

  async openFile(uri: Uri): Promise<ReadHandle> {
    return new HttpsReadHandle(uri);
  }
}


class HttpsReadHandle extends ReadHandle {
  position = 0;
  constructor(private target: Uri) {
    super();
  }

  async read<TBuffer extends Uint8Array>(buffer: TBuffer, offset = 0, length = buffer.byteLength, position: number | null = null): Promise<{ bytesRead: number; buffer: TBuffer; }> {
    if (position !== null) {
      this.position = position;
    }

    const r = getStream(this.target, { start: this.position, end: this.position + length });
    let bytesRead = 0;

    for await (const chunk of r) {
      const c = <Buffer>chunk;
      c.copy(buffer, offset);
      bytesRead += c.length;
      offset += c.length;
    }
    return { bytesRead, buffer };
  }

  async size(): Promise<number> {
    return this.target.size();
  }

  async close() {
    //return this.handle.close();
  }
}
