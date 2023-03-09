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
import { UnpackEvents } from '../interfaces/events';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { UnpackOptions } from './options';
import { implementUnpackOptions } from './unpacker';

export const pipeline = promisify(origPipeline);
// eslint-disable-next-line @typescript-eslint/no-var-requires
const bz2 = <() => Transform>require('unbzip2-stream');

async function maybeUnpackEntry(session: Session, archiveUri: Uri, outputUri: Uri, events: Partial<UnpackEvents>, options: UnpackOptions, header: Headers, stream: Readable): Promise<void> {
  const streamPromise = new Promise((accept, reject) => {
    stream.on('end', accept);
    stream.on('error', reject);
  });

  try {
    const extractPath = implementUnpackOptions(header.name, options);
    let destination: Uri | undefined = undefined;
    if (extractPath) {
      destination = outputUri.join(extractPath);
    }

    if (destination) {
      switch (header?.type) {
        case 'symlink': {
          const linkTargetUri = destination?.parent.join(header.linkname!) || fail('');
          await destination.parent.createDirectory();
          await (<UnifiedFileSystem>session.fileSystem).filesystem(linkTargetUri).createSymlink(linkTargetUri, destination!);
        }
          return;

        case 'link': {
          // this should be a 'hard-link' -- but I'm not sure if we can make hardlinks on windows. todo: find out
          const linkTargetUri = outputUri.join(implementUnpackOptions(header.linkname!, options)!);
          // quick hack
          await destination.parent.createDirectory();
          await (<UnifiedFileSystem>session.fileSystem).filesystem(linkTargetUri).createSymlink(linkTargetUri, destination!);
        }
          return;

        case 'directory':
          session.channels.debug(`in ${archiveUri.fsPath} skipping directory ${header.name}`);
          return;

        case 'file':
          // files handle below
          break;

        default:
          session.channels.warning(i`in ${archiveUri.fsPath} skipping ${header.name} because it is a ${header?.type || ''}`);
          return;
      }

      const fileEntry = {
        archiveUri: archiveUri,
        destination: destination,
        path: header.name,
        extractPath: extractPath
      };

      session.channels.debug(`unpacking TAR ${archiveUri}/${header.name} => ${destination}`);
      events.unpackFileProgress?.(fileEntry, 0);

      if (header.size) {
        const parentDirectory = destination.parent;
        await parentDirectory.createDirectory();
        const fileProgress = new ProgressTrackingStream(0, header.size);
        fileProgress.on('progress', (filePercentage) => events.unpackFileProgress?.(fileEntry, filePercentage));
        const writeStream = await destination.writeStream({ mtime: header.mtime, mode: header.mode });
        await pipeline(stream, fileProgress, writeStream);
      }

      events.unpackFileProgress?.(fileEntry, 100);
      events.unpackFileComplete?.(fileEntry);
    }

  } finally {
    stream.resume();
    await streamPromise;
  }
}

async function getAutoStrip(archiveUri: Uri, decompressorFactory?: () => Transform): Promise<number> {
  const archiveFileStream = await archiveUri.readStream(0, await archiveUri.size());
  let result = 0;
  const folders = new Array<string>();
  const files = new Array<string>();

  const extractor = tarExtract().on('entry', (header, stream, next) => {
    switch (header.type) {
      case 'directory':
        folders.push(header.name);
        break;

      case 'symlink':
      case 'link':
        files.push(header.linkname!);
        break;

      default:
        files.push(header.name);
        break;
    }
    next();
  });


  if (decompressorFactory) {
    await pipeline(archiveFileStream, decompressorFactory(), extractor);
  } else {
    await pipeline(archiveFileStream, extractor);
  }

  for (const folder of folders.sort((a, b) => a.length - b.length)) {
    if (files.all((filename) => filename.startsWith(folder))) {
      result = folder.split('/').length - 1;
      continue;
    }
    break;
  }

  return result;
}

async function unpackTarImpl(session: Session, archiveUri: Uri, outputUri: Uri, events: Partial<UnpackEvents>, options: UnpackOptions, decompressorFactory?: () => Transform): Promise<void> {
  events.unpackArchiveStart?.(archiveUri, outputUri);
  if (options.strip === -1) {
    options.strip = await getAutoStrip(archiveUri, decompressorFactory);
  }

  const archiveSize = await archiveUri.size();
  const archiveFileStream = await archiveUri.readStream(0, archiveSize);
  const archiveProgress = new ProgressTrackingStream(0, archiveSize);
  const tarExtractor = tarExtract();

  tarExtractor.on('entry', (header, stream, next) => {
    return maybeUnpackEntry(session, archiveUri, outputUri, events, options, header, stream).then(() => {
      events.unpackArchiveProgress?.(archiveUri, archiveProgress.currentPercentage);
      next();
    }).catch(err => (<any>next)(err));
  });
  if (decompressorFactory) {
    await pipeline(archiveFileStream, archiveProgress, decompressorFactory(), tarExtractor);
  } else {
    await pipeline(archiveFileStream, archiveProgress, tarExtractor);
  }
}

export function unpackTar(session: Session, archiveUri: Uri, outputUri: Uri, events: Partial<UnpackEvents>, options: UnpackOptions): Promise<void> {
  session.channels.debug(`unpacking TAR ${archiveUri} => ${outputUri}`);
  return unpackTarImpl(session, archiveUri, outputUri, events, options);
}


export function unpackTarGz(session: Session, archiveUri: Uri, outputUri: Uri, events: Partial<UnpackEvents>, options: UnpackOptions): Promise<void> {
  session.channels.debug(`unpacking TAR.GZ ${archiveUri} => ${outputUri}`);
  return unpackTarImpl(session, archiveUri, outputUri, events, options, createGunzip);
}

export function unpackTarBz(session: Session, archiveUri: Uri, outputUri: Uri, events: Partial<UnpackEvents>, options: UnpackOptions): Promise<void> {
  session.channels.debug(`unpacking TAR.BZ2 ${archiveUri} => ${outputUri}`);
  return unpackTarImpl(session, archiveUri, outputUri, events, options, () => {
    const decompressor = bz2();
    (<any>decompressor).autoDestroy = false;
    return decompressor;
  });
}
