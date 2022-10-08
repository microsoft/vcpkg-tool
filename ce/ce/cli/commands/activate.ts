// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { buildRegistryResolver, checkDemands, resolveDependencies } from '../../artifacts/artifact';
import { configurationName } from '../../constants';
import { i } from '../../i18n';
import { trackActivation } from '../../insights';
import { session } from '../../main';
import { showArtifacts } from '../artifacts';
import { Command } from '../command';
import { projectFile } from '../format';
import { activate } from '../project';
import { error } from '../styling';
import { Json } from '../switches/json';
import { MSBuildProps } from '../switches/msbuild-props';
import { Project } from '../switches/project';

export class ActivateCommand extends Command {
  readonly command = 'activate';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  project: Project = new Project(this);
  msbuildProps: MSBuildProps = new MSBuildProps(this);
  json : Json = new Json(this);

  get summary() {
    return i`Activates the tools required for a project`;
  }

  get description() {
    return [
      i`This allows the consumer to Activate the tools required for a project. If the tools are not already installed, this will force them to be downloaded and installed before activation.`,
    ];
  }

  override async run() {
    const projectManifest = await this.project.manifest;

    if (!projectManifest) {
      error(i`Unable to find project in folder (or parent folders) for ${session.currentDirectory.fsPath}`);
      return false;
    }

    const options = {
      force: this.commandLine.force,
      allLanguages: this.commandLine.allLanguages,
      language: this.commandLine.language,
      msbuildProps: this.msbuildProps.resolvedValue,
      json: this.json.resolvedValue
    };

    // track what got installed
    const projectResolver = await buildRegistryResolver(session, projectManifest.metadata.registries);
    if (!checkDemands(session, (await this.project.resolvedValue)?.fsPath ?? configurationName, projectManifest.applicableDemands)) {
      return false;
    }

    const resolved = await resolveDependencies(session, projectResolver, [projectManifest], 3);

    // print the status of what is going to be activated.
    if (!await showArtifacts(resolved, projectResolver, options)) {
      session.channels.error(i`Unable to activate project`);
      return false;
    }

    if (await activate(session, resolved, projectResolver, true, options)) {
      trackActivation();
      session.channels.message(i`Project ${projectFile(projectManifest.metadata.file.parent)} activated`);
      return true;
    }

    session.channels.message(i`Failed to activate project ${projectFile(projectManifest.metadata.file.parent)}`);
    return false;
  }
}
