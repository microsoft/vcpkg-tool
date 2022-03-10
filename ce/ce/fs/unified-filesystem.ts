// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { Readable, Writable } from 'stream';
import { i } from '../i18n';
import { Dictionary } from '../util/linq';
import { Uri } from '../util/uri';
import { FileStat, FileSystem, FileType, ReadHandle, WriteStreamOptions } from './filesystem';

/**
 * gets the scheme off the front of an uri.
 * @param uri the uri to get the scheme for.
 * @returns the scheme, undefined if the uri has no scheme (colon)
 */
export function schemeOf(uri: string) {
  strict.ok(uri, i`Uri may not be empty`);
  return /^(\w*):/.exec(uri)?.[1];
}

export class UnifiedFileSystem extends FileSystem {

  private filesystems: Dictionary<FileSystem> = {};

  /** registers a scheme to a given filesystem
   *
   * @param scheme the Uri scheme to reserve
   * @param fileSystem the filesystem to associate with the scheme
   */
  register(scheme: string | Array<string>, fileSystem: FileSystem) {
    if (Array.isArray(scheme)) {
      for (const each of scheme) {
        this.register(each, fileSystem);
      }
      return this;
    }
    strict.ok(!this.filesystems[scheme], i`scheme '${scheme}' already registered`);
    this.filesystems[scheme] = fileSystem;
    return this;
  }

  /**
   * gets the filesystem for the given uri.
   *
   * @param uri the uri to check the filesystem for
   *
   * @returns the filesystem. Will throw if no filesystem is valid.
   */
  public filesystem(uri: string | Uri) {
    const scheme = schemeOf(uri.toString());

    strict.ok(scheme, i`uri ${uri.toString()} has no scheme`);

    const filesystem = this.filesystems[scheme];
    strict.ok(filesystem, i`scheme ${scheme} has no filesystem associated with it`);

    return filesystem;
  }

  /**
  * Creates a new URI from a string, e.g. `https://www.msft.com/some/path`,
  * `file:///usr/home`, or `scheme:with/path`.
  *
  * @param uri A string which represents an URI (see `URI#toString`).
  */
  override parse(uri: string, _strict?: boolean): Uri {
    return this.filesystem(uri).parse(uri);
  }


  stat(uri: Uri): Promise<FileStat> {
    return this.filesystem(uri).stat(uri);
  }

  async readDirectory(uri: Uri, options?: { recursive?: boolean }): Promise<Array<[Uri, FileType]>> {
    return this.filesystem(uri).readDirectory(uri, options);
  }

  createDirectory(uri: Uri): Promise<void> {
    return this.filesystem(uri).createDirectory(uri);
  }

  readFile(uri: Uri): Promise<Uint8Array> {
    return this.filesystem(uri).readFile(uri);
  }

  openFile(uri: Uri): Promise<ReadHandle> {
    return this.filesystem(uri).openFile(uri);
  }

  writeFile(uri: Uri, content: Uint8Array): Promise<void> {
    return this.filesystem(uri).writeFile(uri, content);
  }

  readStream(uri: Uri, options?: { start?: number, end?: number }): Promise<Readable> {
    return this.filesystem(uri).readStream(uri, options);
  }

  writeStream(uri: Uri, options?: WriteStreamOptions): Promise<Writable> {
    return this.filesystem(uri).writeStream(uri, options);
  }

  delete(uri: Uri, options?: { recursive?: boolean | undefined; useTrash?: boolean | undefined; }): Promise<void> {
    return this.filesystem(uri).delete(uri, options);
  }

  rename(source: Uri, target: Uri, options?: { overwrite?: boolean | undefined; }): Promise<void> {
    strict.ok(source.fileSystem === target.fileSystem, i`may not rename across filesystems`);
    return source.fileSystem.rename(source, target, options);
  }

  copy(source: Uri, target: Uri, options?: { overwrite?: boolean | undefined; }): Promise<number> {
    return target.fileSystem.copy(source, target);
  }

  createSymlink(original: Uri, symlink: Uri): Promise<void> {
    return symlink.fileSystem.createSymlink(original, symlink);
  }
}
