// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { CloneOptions, Git } from '../archivers/git';
import { i } from '../i18n';
import { InstallEvents, InstallOptions } from '../interfaces/events';
import { CloneSettings, GitInstaller } from '../interfaces/metadata/installers/git';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { Vcpkg } from '../vcpkg';

export async function installGit(session: Session, name: string, targetLocation: Uri, install: GitInstaller, events: Partial<InstallEvents>, options: Partial<InstallOptions & CloneOptions & CloneSettings>): Promise<void> {
  const vcpkg = new Vcpkg(session);

  const gitPath = await vcpkg.fetch('git');

  if (!gitPath) {
    throw new Error(i`Git is not installed`);
  }

  const repo = session.parseUri(install.location);
  const targetDirectory = targetLocation.join(options.subdirectory ?? '');

  const gitTool = new Git(session, gitPath, await session.activation.getEnvironmentBlock(), targetDirectory);

  if (! await gitTool.init()) {
    throw new Error(i`Failed to initialize git repository folder (${targetDirectory.fsPath})`);
  }

  if (!await gitTool.addRemote('origin', repo)) {
    throw new Error(i`Failed to set git origin (${repo.toString()}) in folder (${targetDirectory.fsPath})`);
  }

  if (!await gitTool.fetch('origin', events, { commit: install.commit, depth: install.full ? undefined : 1 })) {
    throw new Error(i`Unable to fetch git data for (${repo.toString()}) in folder (${targetDirectory.fsPath})`);
  }

  if (!await gitTool.checkout(events, { commit: "FETCH_HEAD" })) {
    throw new Error(i`Unable to checkout data for (${repo.toString()}) in folder (${targetDirectory.fsPath})`);
  }

  if (install.recurse) {
    if (!await gitTool.config('.gitmodules', 'submodule.*.shallow', 'true')) {
      throw new Error(i`Unable to set submodule shallow data for (${repo.toString()}) in folder (${targetDirectory.fsPath})`);
    }

    if (!await gitTool.updateSubmodules(events, { init: true, recursive: true, depth: install.full ? undefined : 1 })) {
      throw new Error(i`Unable update submodules for (${repo.toString()}) in folder (${targetDirectory.fsPath})`);
    }
  }
}
