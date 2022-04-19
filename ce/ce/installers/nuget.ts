// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ZipUnpacker } from '../archivers/ZipUnpacker';
import { Activation } from '../artifacts/activation';
import { acquireNugetFile } from '../fs/acquire';
import { InstallEvents, InstallOptions } from '../interfaces/events';
import { NupkgInstaller } from '../interfaces/metadata/installers/nupkg';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { applyAcquireOptions } from './util';

export async function installNuGet(session: Session, activation: Activation, name: string, targetLocation: Uri, install: NupkgInstaller, events: Partial<InstallEvents>, options: Partial<InstallOptions>): Promise<void> {
  const file = await acquireNugetFile(session, install.location, `${name}.zip`, events, applyAcquireOptions(options, install));

  return new ZipUnpacker(session).unpack(
    file,
    targetLocation,
    events,
    {
      strip: install.strip,
      transform: [...install.transform],
    });
}
