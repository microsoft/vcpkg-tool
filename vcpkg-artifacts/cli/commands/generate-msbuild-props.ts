// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Activation } from '../../artifacts/activation';
import { buildRegistryResolver, resolveDependencies } from '../../artifacts/artifact';
import { i } from '../../i18n';
import { session } from '../../main';
import { showArtifacts } from '../artifacts';
import { Command } from '../command';
import { error } from '../styling';
import { MSBuildProps } from '../switches/msbuild-props';
import { Project } from '../switches/project';

export class GenerateMSBuildPropsCommand extends Command {
  readonly command = 'generate-msbuild-props';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  project: Project = new Project(this);
  msbuildProps: MSBuildProps = new MSBuildProps(this, 'out');

  get summary() {
    return i`Generates MSBuild properties for an activation without downloading anything for a project`;
  }

  get description() { return ['']; }

  override async run() {
    if (!this.msbuildProps.active) {
      error(i`generate-msbuild-props requires --msbuild-props`);
      return false;
    }

    const projectManifest = await this.project.manifest;

    if (!projectManifest) {
      error(i`Unable to find project in folder (or parent folders) for ${session.currentDirectory.fsPath}`);
      return false;
    }

    const projectResolver = await buildRegistryResolver(session, projectManifest.metadata.registries);
    const resolved = await resolveDependencies(session, projectResolver, [projectManifest], 3);

    // print the status of what is going to be activated.
    if (!await showArtifacts(resolved, projectResolver, {})) {
      error(i`Unable to activate project`);
      return false;
    }

    const activation = await Activation.start(session, false);
    for (const artifact of resolved) {
      if (!await artifact.artifact.loadActivationSettings(activation)) {
        session.channels.error(i`Unable to activate project`);
        return false;
      }
    }

    const content = activation.generateMSBuild();
    await this.msbuildProps.resolvedValue?.writeUTF8(content);
    return true;
  }
}
