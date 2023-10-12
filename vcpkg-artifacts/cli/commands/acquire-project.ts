// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { buildRegistryResolver, resolveDependencies } from '../../artifacts/artifact';
import { i } from '../../i18n';
import { session } from '../../main';
import { acquireArtifacts, showArtifacts } from '../artifacts';
import { Command } from '../command';
import { error } from '../styling';
import { Project } from '../switches/project';

export class AcquireProjectCommand extends Command {
  readonly command = 'acquire-project';
  project: Project = new Project(this);

  override async run() {
    const projectManifest = await this.project.manifest;
    if (!projectManifest) {
      error(i`Unable to find project in folder (or parent folders) for ${session.currentDirectory.fsPath}`);
      return false;
    }

    const projectResolver = await buildRegistryResolver(session, projectManifest.metadata.registries);
    const resolved = await resolveDependencies(session, projectResolver, [projectManifest], 3);

    // print the status of what is going to be acquired
    if (!await showArtifacts(resolved, projectResolver, {force: this.commandLine.force})) {
      session.channels.error(i`Unable to acquire project`);
      return false;
    }

    return await acquireArtifacts(session, resolved, projectResolver, {
      force: this.commandLine.force,
      allLanguages: this.commandLine.allLanguages,
      language: this.commandLine.language
    });
  }
}
