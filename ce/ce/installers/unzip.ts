// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ZipUnpacker } from '../archivers/ZipUnpacker';
import { Activation } from '../artifacts/activation';
import { acquireArtifactFile } from '../fs/acquire';
import { InstallEvents, InstallOptions } from '../interfaces/events';
import { UnZipInstaller } from '../interfaces/metadata/installers/zip';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { applyAcquireOptions, artifactFileName } from './util';

export async function installUnZip(session: Session, activation: Activation, name: string, targetLocation: Uri, install: UnZipInstaller, events: Partial<InstallEvents>, options: Partial<InstallOptions>): Promise<void> {
  const file = await acquireArtifactFile(session, [...install.location].map(each => session.parseUri(each)), artifactFileName(name, install, '.zip'), events, applyAcquireOptions(options, install));
  await new ZipUnpacker(session).unpack(
    file,
    targetLocation,
    events,
    {
      strip: install.strip,
      transform: [...install.transform],
    });
}
