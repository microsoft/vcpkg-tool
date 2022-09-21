// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { buildRegistryResolver, ResolvedArtifact, resolveDependencies } from '../../artifacts/artifact';
import { i } from '../../i18n';
import { session } from '../../main';
import { acquireArtifacts, showArtifacts } from '../artifacts';
import { Command } from '../command';
import { error } from '../styling';
import { AcquisitionTags } from '../switches/acquisition-tags';
import { Project } from '../switches/project';

export class AcquireProjectCommand extends Command {
  readonly command = 'acquire-project';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  project: Project = new Project(this);
  acquisitionTags: AcquisitionTags = new AcquisitionTags(this);

  get summary() {
    return i`Acquires everything referenced by a project, without activating`;
  }

  get description() {
    return [
      i`This allows the consumer to pre-download tools required for a project.`,
    ];
  }

  override async run() {
    const projectManifest = await this.project.manifest;
    if (!projectManifest) {
      error(i`Unable to find project in folder (or parent folders) for ${session.currentDirectory.fsPath}`);
      return false;
    }

    const projectResolver = await buildRegistryResolver(session, projectManifest.metadata.registries);
    const resolved = await resolveDependencies(session, projectResolver, [projectManifest], 3);
    let toAcquire = resolved;
    const activeTagsInput = this.acquisitionTags.value;
    if (activeTagsInput !== undefined) {
      toAcquire = new Array<ResolvedArtifact>();
      const activeTags = new Set<string>(activeTagsInput.split(','));
      activeTags.delete('');
      for (const artifact of resolved) {
        for (const tagInArtifact of artifact.artifact.metadata.acquisitionTags) {
          if (activeTags.has(tagInArtifact)) {
            toAcquire.push(artifact);
            break;
          }
        }
      }
    }

    // print the status of what is going to be acquired
    if (!await showArtifacts(toAcquire, projectResolver, {force: this.commandLine.force})) {
      session.channels.error(i`Unable to acquire project`);
      return false;
    }

    return await acquireArtifacts(session, toAcquire, projectResolver, {
      force: this.commandLine.force,
      allLanguages: this.commandLine.allLanguages,
      language: this.commandLine.language
    });
  }
}
