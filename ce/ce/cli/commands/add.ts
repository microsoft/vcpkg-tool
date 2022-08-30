// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { selectArtifacts } from '../artifacts';
import { Command } from '../command';
import { cmdSwitch } from '../format';
import { activateProject } from '../project';
import { error } from '../styling';
import { Project } from '../switches/project';
import { Version } from '../switches/version';
import { WhatIf } from '../switches/whatIf';

export class AddCommand extends Command {
  readonly command = 'add';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];

  version = new Version(this);
  project: Project = new Project(this);
  whatIf = new WhatIf(this);

  get summary() {
    return i`Adds an artifact to the project`;
  }

  get description() {
    return [
      i`This allows the consumer to add an artifact to the project. This will activate the project as well.`,
    ];
  }

  override async run() {
    const projectManifest = await this.project.manifest;

    if (!projectManifest) {
      error(i`Unable to find project in folder (or parent folders) for ${session.currentDirectory.fsPath}`);
      return false;
    }

    if (this.inputs.length === 0) {
      error(i`No artifacts specified`);
      return false;
    }

    const versions = this.version.values;
    if (versions.length && this.inputs.length !== versions.length) {
      error(i`Multiple artifacts specified, but not an equal number of ${cmdSwitch('version')} switches`);
      return false;
    }

    const selections = new Map(this.inputs.map((v, i) => [v, versions[i] || '*']));
    const projectRegistries = projectManifest.registries;
    const selectedArtifacts = await selectArtifacts(selections, projectRegistries);

    if (!selectedArtifacts) {
      return false;
    }

    for (const [artifact, id, requested] of selectedArtifacts.values()) {
      // map the registry of the found artifact to the registries already in the project file
      const registryUri = artifact.registryUri;
      let registry = projectRegistries.getRegistry(registryUri);
      let registryId : string;
      if (registry) {
        // the registry is already declared in the project, so get the name to use there
        registryId = projectRegistries.getRegistryDisplayName(registryUri);
      } else {
        // the registry isn't known yet to the project, try to declare it
        registry = session.registries.getRegistry(artifact.registryUri);
        registryId = session.registries.getRegistryDisplayName(artifact.registryUri);
        if (!registry || !registryId) {
          throw new Error(i`Tried to add an artifact [${registryUri.toString()}]:${artifact.id} but could not determine the registry to use.`);
        }

        const conflictingRegistry = projectRegistries.getRegistry(registryId);
        if (conflictingRegistry) {
          throw new Error(i`Tried to add registry ${registryId} as ${registryUri.toString()}, but it was already ${conflictingRegistry.location.toString()}. Please add ${registryUri.toString()} to this project manually and reattempt.`);
        }

        projectManifest.metadata.registries.add(registryId, artifact.registryUri, 'artifact');
      }

      // add the artifact to the project
      const fulfilled = artifact.version.toString();
      const v = requested !== fulfilled ? `${requested} ${fulfilled}` : fulfilled;
      projectManifest.metadata.requires.set(`${registryId}:${artifact.id}`, <any>v);
    }

    // write the file out.
    await projectManifest.metadata.save();

    return await activateProject(projectManifest, this.commandLine);
  }
}
