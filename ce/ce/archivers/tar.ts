// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { fail } from 'assert';
import { pipeline as origPipeline, Readable, Transform } from 'stream';
import { extract as tarExtract, Headers } from 'tar-stream';
import { promisify } from 'util';
import { createGunzip } from 'zlib';
import { ProgressTrackingStream } from '../fs/streams';
import { UnifiedFileSystem } from '../fs/unified-filesystem';
import { i } from '../i18n';
import { InstallEvents } from '../interfaces/events';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { UnpackOptions } from './options';
import { Unpacker } from './unpacker';

export const pipeline = promisify(origPipeline);
// eslint-disable-next-line @typescript-eslint/no-var-requires
const bz2 = require('unbzip2-stream');

abstract class BasicTarUnpacker extends Unpacker {
  constructor(protected readonly session: Session) {
    super();
  }

  async maybeUnpackEntry(archiveUri: Uri, outputUri: Uri, events: Partial<InstallEvents>, options: UnpackOptions, header: Headers, stream: Readable): Promise<void> {
    const streamPromise = new Promise((accept, reject) => {
      stream.on('end', accept);
      stream.on('error', reject);
    });

    try {
      const extractPath = Unpacker.implementOutputOptions(header.name, options);
      let destination: Uri | undefined = undefined;
      if (extractPath) {
        destination = outputUri.join(extractPath);
      }

      if (destination) {
        switch (header?.type) {
          case 'symlink': {
            const linkTargetUri = destination?.parent.join(header.linkname!) || fail('');
            await destination.parent.createDirectory();
            await (<UnifiedFileSystem>this.session.fileSystem).filesystem(linkTargetUri).createSymlink(linkTargetUri, destination!);
          }
            return;

          case 'link': {
            // this should be a 'hard-link' -- but I'm not sure if we can make hardlinks on windows. todo: find out
            const linkTargetUri = outputUri.join(Unpacker.implementOutputOptions(header.linkname!, options)!);
            // quick hack
            await destination.parent.createDirectory();
            await (<UnifiedFileSystem>this.session.fileSystem).filesystem(linkTargetUri).createSymlink(linkTargetUri, destination!);
          }
            return;

          case 'directory':
            this.session.channels.debug(`in ${archiveUri.fsPath} skipping directory ${header.name}`);
            return;

          case 'file':
            // files handle below
            break;

          default:
            this.session.channels.warning(i`in ${archiveUri.fsPath} skipping ${header.name} because it is a ${header?.type || ''}`);
            return;
        }

        const fileEntry = {
          archiveUri: archiveUri,
          destination: destination,
          path: header.name,
          extractPath: extractPath
        };

        this.session.channels.debug(`unpacking TAR ${archiveUri}/${header.name} => ${destination}`);
        this.fileProgress(fileEntry, 0);

        if (header.size) {
          const parentDirectory = destination.parent;
          await parentDirectory.createDirectory();
          const fileProgress = new ProgressTrackingStream(0, header.size);
          fileProgress.on('progress', (filePercentage) => this.fileProgress(fileEntry, filePercentage));
          fileProgress.on('progress', (filePercentage) => events?.fileProgress?.(fileEntry, filePercentage));
          const writeStream = await destination.writeStream({ mtime: header.mtime, mode: header.mode });
          await pipeline(stream, fileProgress, writeStream);
        }

        this.fileProgress(fileEntry, 100);
        this.unpacked(fileEntry);
      }

    } finally {
      stream.resume();
      await streamPromise;
    }
  }

  protected async unpackTar(archiveUri: Uri, outputUri: Uri, events: Partial<InstallEvents>, options: UnpackOptions, decompressor?: Transform): Promise<void> {
    this.subscribe(events);
    const archiveSize = await archiveUri.size();
    const archiveFileStream = await archiveUri.readStream(0, archiveSize);
    const archiveProgress = new ProgressTrackingStream(0, archiveSize);
    const tarExtractor = tarExtract();

    tarExtractor.on('entry', (header, stream, next) =>
      this.maybeUnpackEntry(archiveUri, outputUri, events, options, header, stream).then(() => {
        this.progress(archiveProgress.currentPercentage);
        next();
      }).catch(err => (<any>next)(err)));

    if (decompressor) {
      await pipeline(archiveFileStream, archiveProgress, decompressor, tarExtractor);
    } else {
      await pipeline(archiveFileStream, archiveProgress, tarExtractor);
    }
  }
}

export class TarUnpacker extends BasicTarUnpacker {
  unpack(archiveUri: Uri, outputUri: Uri, events: Partial<InstallEvents>, options: UnpackOptions): Promise<void> {
    this.session.channels.debug(`unpacking TAR ${archiveUri} => ${outputUri}`);
    return this.unpackTar(archiveUri, outputUri, events, options);
  }
}

export class TarGzUnpacker extends BasicTarUnpacker {
  unpack(archiveUri: Uri, outputUri: Uri, events: Partial<InstallEvents>, options: UnpackOptions): Promise<void> {
    this.session.channels.debug(`unpacking TAR.GZ ${archiveUri} => ${outputUri}`);
    return this.unpackTar(archiveUri, outputUri, events, options, createGunzip());
  }
}

export class TarBzUnpacker extends BasicTarUnpacker {
  unpack(archiveUri: Uri, outputUri: Uri, events: Partial<InstallEvents>, options: UnpackOptions): Promise<void> {
    this.session.channels.debug(`unpacking TAR.BZ2 ${archiveUri} => ${outputUri}`);
    return this.unpackTar(archiveUri, outputUri, events, options, bz2());
  }
}
