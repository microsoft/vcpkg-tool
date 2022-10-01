// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { unpackTar, unpackTarBz, unpackTarGz } from '../archivers/tar';
import { Unpacker } from '../archivers/unpacker';
import { acquireArtifactFile } from '../fs/acquire';
import { InstallEvents, InstallOptions } from '../interfaces/events';
import { UnTarInstaller } from '../interfaces/metadata/installers/tar';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { applyAcquireOptions, artifactFileName } from './util';


export async function installUnTar(session: Session, name: string, version: string, targetLocation: Uri, install: UnTarInstaller, events: Partial<InstallEvents>, options: Partial<InstallOptions>): Promise<void> {
  const file = await acquireArtifactFile(session, [...install.location].map(each => session.parseLocation(each)), artifactFileName(name, version, install, '.tar'), events, applyAcquireOptions(options, install));
  const x = await file.readBlock(0, 128);
  let unpacker: Unpacker;
  if (x[0] === 0x1f && x[1] === 0x8b) {
    unpacker = unpackTarGz;
  } else if (x[0] === 66 && x[1] === 90) {
    unpacker = unpackTarBz;
  } else {
    unpacker = unpackTar;
  }

  return unpacker(session, file, targetLocation, events, { strip: install.strip, transform: [...install.transform] });
}
