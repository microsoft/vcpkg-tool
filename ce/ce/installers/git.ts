// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { CloneOptions, Git } from '../archivers/git';
import { i } from '../i18n';
import { InstallEvents, InstallOptions } from '../interfaces/events';
import { CloneSettings, GitInstaller } from '../interfaces/metadata/installers/git';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { vcpkgFetch } from '../vcpkg';

export async function installGit(session: Session, name: string, version: string, targetLocation: Uri, install: GitInstaller, events: Partial<InstallEvents>, options: Partial<InstallOptions & CloneOptions & CloneSettings>): Promise<void> {
  const gitPath = await vcpkgFetch(session, 'git');

  if (!gitPath) {
    throw new Error(i`Git is not installed`);
  }

  const repo = session.parseLocation(install.location);
  const targetDirectory = targetLocation.join(options.subdirectory ?? '');

  const gitTool = new Git(gitPath, targetDirectory);
  events.unpackArchiveStart?.(repo, targetDirectory);

  // changing the clone process to do an init/add remote/fetch/checkout because
  // it's far faster to clone a specific commit and this allows us to support
  // recursive shallow submodules as well.

  if (! await gitTool.init()) {
    events.unpackArchiveHeartbeat?.(i`Initializing repository folder`);
    throw new Error(i`Failed to initialize git repository folder (${targetDirectory.fsPath})`);
  }

  if (!await gitTool.addRemote('origin', repo)) {
    events.unpackArchiveHeartbeat?.(i`Adding remote ${repo.toString()} to git repository folder`);
    throw new Error(i`Failed to set git origin (${repo.toString()}) in folder (${targetDirectory.fsPath})`);
  }

  if (!await gitTool.fetch('origin', events, { commit: install.commit, depth: install.full ? undefined : 1 })) {
    events.unpackArchiveHeartbeat?.(i`Fetching remote ${repo.toString()} for git repository folder`);
    throw new Error(i`Unable to fetch git data for (${repo.toString()}) in folder (${targetDirectory.fsPath})`);
  }

  if (!await gitTool.checkout(events, { commit: 'FETCH_HEAD' })) {
    events.unpackArchiveHeartbeat?.(i`Checking out commit ${install.commit} for ${repo.toString()} to git repository folder`);
    throw new Error(i`Unable to checkout data for (${repo.toString()}) in folder (${targetDirectory.fsPath})`);
  }

  if (install.recurse) {
    events.unpackArchiveHeartbeat?.(i`Updating submodules for repository ${repo.toString()} in the git repository folder`);
    if (!await gitTool.config('.gitmodules', 'submodule.*.shallow', 'true')) {
      throw new Error(i`Unable to set submodule shallow data for (${repo.toString()}) in folder (${targetDirectory.fsPath})`);
    }

    if (!await gitTool.updateSubmodules(events, { init: true, recursive: true, depth: install.full ? undefined : 1 })) {
      throw new Error(i`Unable update submodules for (${repo.toString()}) in folder (${targetDirectory.fsPath})`);
    }
  }
}
