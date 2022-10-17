// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Artifact, buildRegistryResolver } from '../../artifacts/artifact';
import { i } from '../../i18n';
import { session } from '../../main';
import { selectArtifacts, showArtifacts } from '../artifacts';
import { Command } from '../command';
import { cmdSwitch } from '../format';
import { error } from '../styling';
import { Project } from '../switches/project';
import { Version } from '../switches/version';

export class AddCommand extends Command {
  readonly command = 'add';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];

  version = new Version(this);
  project: Project = new Project(this);

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
    const projectResolver = await buildRegistryResolver(session, projectManifest.metadata.registries);
    const combinedResolver = session.globalRegistryResolver.with(projectResolver);
    const selectedArtifacts = await selectArtifacts(session, selections, combinedResolver, 1);
    if (!selectedArtifacts) {
      return false;
    }

    await showArtifacts(selectedArtifacts, combinedResolver);
    for (const resolution of selectedArtifacts) {
      // map the registry of the found artifact to the registries already in the project file
      const artifact = resolution.artifact;
      if (resolution.initialSelection && artifact instanceof Artifact) {
        const registryUri = artifact.metadata.registryUri!;
        let registryName = projectResolver.getRegistryName(registryUri);
        if (!registryName) {
          // the registry isn't known yet to the project, try to declare it
          registryName = session.globalRegistryResolver.getRegistryName(registryUri);
          if (!registryName) {
            throw new Error(i`Tried to add an artifact [${registryUri.toString()}]:${artifact.id} but could not determine the registry to use.`);
          }

          const conflictingRegistry = projectResolver.getRegistryByName(registryName);
          if (conflictingRegistry) {
            throw new Error(i`Tried to add registry ${registryName} as ${registryUri.toString()}, but it was already ${conflictingRegistry.location.toString()}. Please add ${registryUri.toString()} to this project manually and reattempt.`);
          }

          projectManifest.metadata.registries.add(registryName, artifact.registryUri, 'artifact');
          projectResolver.add(registryUri, registryName);
        }

        // add the artifact to the project
        const fulfilled = artifact.version.toString();
        const requested = resolution.requestedVersion;
        const v = requested !== fulfilled ? `${requested} ${fulfilled}` : fulfilled;
        projectManifest.metadata.requires.set(`${registryName}:${artifact.id}`, <any>v);
      }
    }

    // write the file out.
    await projectManifest.metadata.save();
    session.channels.message(i`Run \`vcpkg activate\` to apply to the current terminal`);
    return true;
  }
}
