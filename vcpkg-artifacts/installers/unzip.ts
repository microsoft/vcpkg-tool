// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { acquireArtifactFile } from '../fs/acquire';
import { InstallEvents, InstallOptions } from '../interfaces/events';
import { UnZipInstaller } from '../interfaces/metadata/installers/zip';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { vcpkgExtract } from '../vcpkg';
import { applyAcquireOptions, artifactFileName } from './util';

export async function installUnZip(session: Session, name: string, version: string, targetLocation: Uri, install: UnZipInstaller, events: Partial<InstallEvents>, options: Partial<InstallOptions>): Promise<void> {
  const file = await acquireArtifactFile(session, [...install.location].map(each => session.parseLocation(each)), artifactFileName(name, version, install, '.zip'), events, applyAcquireOptions(options, install));
  await vcpkgExtract(
    session,
    file.fsPath,
    targetLocation.fsPath,
   "--strip=" + install.strip);
}