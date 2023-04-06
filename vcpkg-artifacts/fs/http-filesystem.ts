// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Readable, Writable } from 'stream';
import { Uri } from '../util/uri';
import { FileStat, FileSystem, FileType, ReadHandle } from './filesystem';

/**
 * HTTPS Filesystem
 *
 */
export class HttpsFileSystem extends FileSystem {

  async stat(uri: Uri): Promise<FileStat> {
    throw new Error('Method not implemented');
  }
  readDirectory(uri: Uri): Promise<Array<[Uri, FileType]>> {
    throw new Error('Method not implemented');
  }
  createDirectory(uri: Uri): Promise<void> {
    throw new Error('Method not implemented');
  }
  async readFile(uri: Uri): Promise<Uint8Array> {
    throw new Error('Method not implemented');
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
    throw new Error('Method not implemented');
  }
  writeStream(uri: Uri): Promise<Writable> {
    throw new Error('Method not implemented');
  }

  async openFile(uri: Uri): Promise<ReadHandle> {
    throw new Error('Method not implemented');
  }
}
