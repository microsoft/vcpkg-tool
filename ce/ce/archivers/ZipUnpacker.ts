// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ProgressTrackingStream } from '../fs/streams';
import { InstallEvents } from '../interfaces/events';
import { Session } from '../session';
import { PercentageScaler } from '../util/percentage-scaler';
import { Queue } from '../util/promise';
import { Uri } from '../util/uri';
import { UnpackOptions } from './options';
import { pipeline, Unpacker } from './unpacker';
import { ZipEntry, ZipFile } from './unzip';

export class ZipUnpacker extends Unpacker {
  constructor(private readonly session: Session) {
    super();
  }

  async unpackFile(file: ZipEntry, archiveUri: Uri, outputUri: Uri, options: UnpackOptions) {
    const extractPath = Unpacker.implementOutputOptions(file.name, options);
    if (extractPath) {
      const destination = outputUri.join(extractPath);
      const fileEntry = {
        archiveUri,
        destination,
        path: file.name,
        extractPath
      };

      this.fileProgress(fileEntry, 0);
      this.session.channels.debug(`unpacking ZIP file ${archiveUri}/${file.name} => ${destination}`);
      await destination.parent.createDirectory();
      const readStream = await file.read();
      const mode = ((file.attr >> 16) & 0xfff);

      const writeStream = await destination.writeStream({ mtime: file.time, mode: mode ? mode : undefined });
      const progressStream = new ProgressTrackingStream(0, file.size);
      progressStream.on('progress', (filePercentage) => this.fileProgress(fileEntry, filePercentage));
      await pipeline(readStream, progressStream, writeStream);
      this.fileProgress(fileEntry, 100);
      this.unpacked(fileEntry);
    }
  }

  async unpack(archiveUri: Uri, outputUri: Uri, events: Partial<InstallEvents>, options: UnpackOptions): Promise<void> {
    this.subscribe(events);
    try {
      this.session.channels.debug(`unpacking ZIP ${archiveUri} => ${outputUri}`);

      const openedFile = await archiveUri.openFile();
      const zipFile = await ZipFile.read(openedFile);

      const archiveProgress = new PercentageScaler(0, zipFile.files.size);
      this.progress(0);
      const q = new Queue();
      let count = 0;
      for (const file of zipFile.files.values()) {

        void q.enqueue(async () => {
          await this.unpackFile(file, archiveUri, outputUri, options);
          this.progress(archiveProgress.scalePosition(count++));
        });
      }
      await q.done;
      await zipFile.close();
      this.progress(100);
    } finally {
      this.unsubscribe(events);
    }
  }
}
