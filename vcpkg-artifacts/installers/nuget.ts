// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { acquireNugetFile } from '../fs/acquire';
import { InstallEvents, InstallOptions } from '../interfaces/events';
import { NupkgInstaller } from '../interfaces/metadata/installers/nupkg';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { vcpkgExtract } from '../vcpkg';
import { applyAcquireOptions } from './util';
export async function installNuGet(session: Session, name: string, version: string, targetLocation: Uri, install: NupkgInstaller, events: Partial<InstallEvents>, options: Partial<InstallOptions>): Promise<void> {
  const file = await acquireNugetFile(session, install.location, `${name}.zip`, events, applyAcquireOptions(options, install));
  await vcpkgExtract(
    session,
    file.fsPath,
    targetLocation.fsPath,
   "--strip=" + install.strip);
}