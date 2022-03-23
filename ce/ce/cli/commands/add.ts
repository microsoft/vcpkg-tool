// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { selectArtifacts } from '../artifacts';
import { Command } from '../command';
import { cmdSwitch, projectFile } from '../format';
import { activateProject } from '../project';
import { debug, error } from '../styling';
import { Project } from '../switches/project';
import { Registry } from '../switches/registry';
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
  registrySwitch = new Registry(this);


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

    // pull in any registries that are on the command line
    await this.registrySwitch.loadRegistries(session);

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
    const selectedArtifacts = await selectArtifacts(selections, projectManifest.registries);

    if (!selectedArtifacts) {
      return false;
    }

    for (const [artifact, id, requested] of selectedArtifacts.values()) {
      // make sure the registry is in the project
      const registry = projectManifest.registries.getRegistry(artifact.registryUri);
      if (!registry) {
        const r = projectManifest.metadata.registries.add(artifact.registryId, artifact.registryUri, 'artifact');

      }


      // add the artifact to the project
      const fulfilled = artifact.version.toString();
      const v = requested !== fulfilled ? `${requested} ${fulfilled}` : fulfilled;
      projectManifest.metadata.requires.set(artifact.reference, <any>v);
    }

    // write the file out.
    await projectManifest.metadata.save();

    debug(i`Deactivating project ${projectFile(projectManifest.metadata.context.file)}`);
    await session.deactivate();

    return await activateProject(projectManifest, this.commandLine);
  }
}