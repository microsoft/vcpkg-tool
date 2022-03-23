// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { COPYFILE_EXCL } from 'constants';
import { close, createReadStream, createWriteStream, futimes, NoParamCallback, open as openFd, Stats, write as writeFd, writev as writevFd } from 'fs';
import { copyFile, FileHandle, mkdir, open, readdir, readFile, rename, rm, stat, symlink, writeFile } from 'fs/promises';
import { basename, join } from 'path';
import { Readable, Writable } from 'stream';
import { i } from '../i18n';
import { delay } from '../util/events';
import { TargetFileCollision } from '../util/exceptions';
import { Queue } from '../util/promise';
import { Uri } from '../util/uri';
import { FileStat, FileSystem, FileType, ReadHandle, WriteStreamOptions } from './filesystem';

function getFileType(stats: Stats) {
  return FileType.Unknown |
    (stats.isDirectory() ? FileType.Directory : 0) |
    (stats.isFile() ? FileType.File : 0) |
    (stats.isSymbolicLink() ? FileType.SymbolicLink : 0);
}

class LocalFileStats implements FileStat {
  constructor(private stats: Stats) {
    strict.ok(stats, i`stats may not be undefined`);
  }
  get type() {
    return getFileType(this.stats);
  }
  get ctime() {
    return this.stats.ctimeMs;
  }
  get mtime() {
    return this.stats.mtimeMs;
  }
  get size() {
    return this.stats.size;
  }
  get mode() {
    return this.stats.mode;
  }
}


/**
 * Implementation of the Local File System
 *
 * This is used to handle the access to the local disks.
 */
export class LocalFileSystem extends FileSystem {
  async stat(uri: Uri): Promise<FileStat> {
    const path = uri.fsPath;
    const s = await stat(path);
    return new LocalFileStats(s);
  }

  async readDirectory(uri: Uri, options?: { recursive?: boolean }): Promise<Array<[Uri, FileType]>> {
    let retval!: Promise<Array<[Uri, FileType]>>;
    try {
      const folder = uri.fsPath;
      const retval = new Array<[Uri, FileType]>();

      // use forEachAsync instead so we can throttle this appropriately.
      await (await readdir(folder)).forEachAsync(async each => {
        const path = uri.fileSystem.file(join(folder, each));
        const type = getFileType(await stat(uri.join(each).fsPath));
        retval.push(<[Uri, FileType]>[path, type]);
        if (options?.recursive && type === FileType.Directory) {
          retval.push(... await this.readDirectory(path, options));
        }
      }).done;

      return retval;
    } finally {
      // log that.
      this.directoryRead(uri, retval);
    }
  }

  async createDirectory(uri: Uri): Promise<void> {
    await mkdir(uri.fsPath, { recursive: true });
    this.directoryCreated(uri);
  }

  createSymlink(original: Uri, slink: Uri): Promise<void> {
    return symlink(original.fsPath, slink.fsPath, 'file');
  }

  async readFile(uri: Uri): Promise<Uint8Array> {
    let contents!: Promise<Uint8Array>;
    try {
      contents = readFile(uri.fsPath);
      return await contents;
    } finally {
      this.read(uri, contents);
    }
  }

  async writeFile(uri: Uri, content: Uint8Array): Promise<void> {
    try {
      await uri.parent.createDirectory();
      return writeFile(uri.fsPath, content);
    } finally {
      this.write(uri, content);
    }
  }

  async delete(uri: Uri, options?: { recursive?: boolean | undefined; useTrash?: boolean | undefined; }): Promise<void> {
    try {
      options = options || { recursive: false };
      await rm(uri.fsPath, { recursive: options.recursive, force: true, maxRetries: 3, retryDelay: 20 });
      // todo: Hack -- on windows, when something is used and then deleted, the delete might not actually finish
      // before the Promise is resolved. Adding a delay fixes this (but probably is an underlying node bug)
      await delay(50);
      return;
    } finally {
      this.deleted(uri);
    }
  }

  rename(source: Uri, target: Uri, options?: { overwrite?: boolean | undefined; }): Promise<void> {
    try {
      strict.equal(source.fileSystem, target.fileSystem, i`Cannot rename files across filesystems`);
      return rename(source.fsPath, target.fsPath);
    } finally {
      this.renamed(source, { target, options });
    }
  }

  async copy(source: Uri, target: Uri, options?: { overwrite?: boolean | undefined; }): Promise<number> {
    const { type } = await source.stat();
    const opts = <any>(options || {});
    const overwrite = opts.overwrite ? 0 : COPYFILE_EXCL;

    if (type & FileType.File) {
      // make sure the target folder is there
      await target.parent.createDirectory();
      await copyFile(source.fsPath, target.fsPath, overwrite);
      return 1;
    }

    strict.ok(type & FileType.Directory, 'Unknown file type should never happen during copy');

    let targetIsFile = false;
    try {
      targetIsFile = !!((await target.stat()).type & FileType.File);
    } catch {
      // not a file
    }

    // if it's a folder, then the target has to be a folder, or not exist
    if (targetIsFile) {
      throw new TargetFileCollision(target, i`Copy failed: source (${source.fsPath}) is a folder, target (${target.fsPath}) is a file`);
    }

    // make sure the target folder exists
    await target.createDirectory();

    // only the initial call gets to wait for everybody to finish.
    let queue: Queue | undefined;

    // track the count, starting at the base folder.
    if (opts.queue === undefined) {
      queue = opts.queue = new Queue();
    }

    // loop thru the contents of this folder
    for (const [sourceUri, fileType] of await source.readDirectory()) {
      const targetUri = target.join(basename(sourceUri.path));
      if (fileType & FileType.Directory) {
        await this.copy(sourceUri, targetUri, opts);
        continue;
      }
      // queue up the copy file
      void opts.queue.enqueue(() => copyFile(sourceUri.fsPath, targetUri.fsPath, overwrite));
    }
    return queue ? queue.done : -1 /* innerloop */;
  }

  async readStream(uri: Uri, options?: { start?: number, end?: number }): Promise<Readable> {
    this.read(uri);
    return createReadStream(uri.fsPath, options);
  }

  async writeStream(uri: Uri, options?: WriteStreamOptions): Promise<Writable> {
    this.write(uri);
    const flags = options?.append ? 'a' : 'w';
    const createWriteOptions: any = { flags, mode: options?.mode, autoClose: true, emitClose: true };
    if (options?.mtime) {
      const mtime = options.mtime;
      // inject futimes call as part of close
      createWriteOptions.fs = {
        open: openFd,
        write: writeFd,
        writev: writevFd,
        close: (fd: number, callback: NoParamCallback) => {
          futimes(fd, new Date(), mtime, (futimesErr) => {
            close(fd, (closeErr) => {
              callback(futimesErr || closeErr);
            });
          });
        }
      };
    }

    return createWriteStream(uri.fsPath, createWriteOptions);
  }

  async openFile(uri: Uri): Promise<ReadHandle> {
    return new LocalReadHandle(await open(uri.fsPath, 'r'));
  }
}

class LocalReadHandle extends ReadHandle {
  constructor(private handle: FileHandle) {
    super();
  }

  read<TBuffer extends Uint8Array>(buffer: TBuffer, offset = 0, length = buffer.byteLength, position: number | null = null): Promise<{ bytesRead: number; buffer: TBuffer; }> {
    return this.handle.read(buffer, offset, length, position);
  }

  async size(): Promise<number> {
    const stat = await this.handle.stat();
    return stat.size;
  }

  async close() {
    return this.handle.close();
  }
}
