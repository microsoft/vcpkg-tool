// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { buildRegistryResolver, resolveDependencies } from '../../artifacts/artifact';
import { i } from '../../i18n';
import { session } from '../../main';
import { acquireArtifacts, showArtifacts } from '../artifacts';
import { Command } from '../command';

export class AcquireProjectCommand extends Command {
  readonly command = 'acquire-project';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  get summary() {
    return i`Acquires everything referenced by a project, without activating`;
  }

  get description() {
    return [
      i`This allows the consumer to pre-download tools required for a project.`,
    ];
  }

  override async run() {
    const projectManifest = await session.loadRequiredProjectProfile();
    if (!projectManifest) {
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
