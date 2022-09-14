// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ProgressTrackingStream } from '../fs/streams';
import { UnpackEvents } from '../interfaces/events';
import { Session } from '../session';
import { PercentageScaler } from '../util/percentage-scaler';
import { Queue } from '../util/promise';
import { Uri } from '../util/uri';
import { UnpackOptions } from './options';
import { implementUnpackOptions, pipeline } from './unpacker';
import { ZipEntry, ZipFile } from './unzip';

async function unpackFile(session: Session, file: ZipEntry, archiveUri: Uri, outputUri: Uri, events: Partial<UnpackEvents>, options: UnpackOptions) {
  const extractPath = implementUnpackOptions(file.name, options);
  if (extractPath) {
    const destination = outputUri.join(extractPath);
    const fileEntry = {
      archiveUri,
      destination,
      path: file.name,
      extractPath
    };

    events.unpackFileProgress?.(fileEntry, 0);
    session.channels.debug(`unpacking ZIP file ${archiveUri}/${file.name} => ${destination}`);
    await destination.parent.createDirectory();
    const readStream = await file.read();
    const mode = ((file.attr >> 16) & 0xfff);

    const writeStream = await destination.writeStream({ mtime: file.time, mode: mode ? mode : undefined });
    const progressStream = new ProgressTrackingStream(0, file.size);
    progressStream.on('progress', (filePercentage) => events.unpackFileProgress?.(fileEntry, filePercentage));
    await pipeline(readStream, progressStream, writeStream);
    events.unpackFileProgress?.(fileEntry, 100);
    events.unpackFileComplete?.(fileEntry);
  }
}

export async function unpackZip(session: Session, archiveUri: Uri, outputUri: Uri, events: Partial<UnpackEvents>, options: UnpackOptions): Promise<void> {
  session.channels.debug(`unpacking ZIP ${archiveUri} => ${outputUri}`);
  events.unpackArchiveStart?.(archiveUri, outputUri);
  const openedFile = await archiveUri.openFile();
  const zipFile = await ZipFile.read(openedFile);
  if (options.strip === -1) {
    // when strip == -1, strip off all common folders off the front of the file names
    options.strip = 0;
    const folders = [...zipFile.folders.keys()].sort((a, b) => a.length - b.length);
    const files = [...zipFile.files.keys()];
    for (const folder of folders) {
      if (files.all((filename) => filename.startsWith(folder))) {
        options.strip = folder.split('/').length - 1;
        continue;
      }
      break;
    }
  }

  const archiveProgress = new PercentageScaler(0, zipFile.files.size);
  events.unpackArchiveProgress?.(archiveUri, 0);
  const q = new Queue();
  let count = 0;
  for (const file of zipFile.files.values()) {
    void q.enqueue(async () => {
      await unpackFile(session, file, archiveUri, outputUri, events, options);
      events.unpackArchiveProgress?.(archiveUri, archiveProgress.scalePosition(count++));
    });
  }

  await q.done;
  await zipFile.close();
  events.unpackArchiveProgress?.(archiveUri, 100);
}
