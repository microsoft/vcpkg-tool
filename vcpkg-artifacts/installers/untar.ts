// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { acquireArtifactFile } from '../fs/acquire';
import { InstallEvents, InstallOptions } from '../interfaces/events';
import { UnTarInstaller } from '../interfaces/metadata/installers/tar';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { vcpkgExtract } from '../vcpkg';
import { applyAcquireOptions, artifactFileName } from './util';

export async function installUnTar(session: Session, name: string, version: string, targetLocation: Uri, install: UnTarInstaller, events: Partial<InstallEvents>, options: Partial<InstallOptions>): Promise<void> {
  const file = await acquireArtifactFile(session, [...install.location].map(each => session.parseLocation(each)), artifactFileName(name, version, install, '.tar'), events, applyAcquireOptions(options, install));
  events.unpackArchiveStart?.(file);
  await vcpkgExtract(
    session,
    file.fsPath,
    targetLocation.fsPath,
    install.strip
  );
}
