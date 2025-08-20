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

  async stat(_uri: Uri): Promise<FileStat> {
    throw new Error('Method not implemented');
  }
  readDirectory(_uri: Uri): Promise<Array<[Uri, FileType]>> {
    throw new Error('Method not implemented');
  }
  createDirectory(_uri: Uri): Promise<void> {
    throw new Error('Method not implemented');
  }
  async readFile(_uri: Uri): Promise<Uint8Array> {
    throw new Error('Method not implemented');
  }
  writeFile(_uri: Uri, _content: Uint8Array): Promise<void> {
    throw new Error('Method not implemented');
  }
  delete(_uri: Uri, _options?: { recursive?: boolean | undefined; useTrash?: boolean | undefined; }): Promise<void> {
    throw new Error('Method not implemented');
  }
  rename(_source: Uri, _target: Uri, _options?: { overwrite?: boolean | undefined; }): Promise<void> {
    throw new Error('Method not implemented');
  }
  copy(_source: Uri, _target: Uri, _options?: { overwrite?: boolean | undefined; }): Promise<number> {
    throw new Error('Method not implemented');
  }
  async createSymlink(_original: Uri, _symlink: Uri): Promise<void> {
    throw new Error('Method not implemented');
  }
  async readStream(_uri: Uri, _options?: { start?: number, end?: number }): Promise<Readable> {
    throw new Error('Method not implemented');
  }
  writeStream(_uri: Uri): Promise<Writable> {
    throw new Error('Method not implemented');
  }

  async openFile(_uri: Uri): Promise<ReadHandle> {
    throw new Error('Method not implemented');
  }
}
