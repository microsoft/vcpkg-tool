// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { projectFile } from '../format';
import { activateProject } from '../project';
import { debug, error } from '../styling';
import { MSBuildProps } from '../switches/msbuild-props';
import { Project } from '../switches/project';
import { WhatIf } from '../switches/whatIf';

export class ActivateCommand extends Command {
  readonly command = 'activate';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  whatIf = new WhatIf(this);
  project: Project = new Project(this);
  msbuildProps: MSBuildProps = new MSBuildProps(this);

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

    debug(i`Deactivating project ${projectFile(projectManifest.metadata.context.file)}`);
    await session.deactivate();

    return await activateProject(projectManifest, {
      force: this.commandLine.force,
      allLanguages: this.commandLine.allLanguages,
      language: this.commandLine.language,
      msbuildProps: await this.msbuildProps.value
    });
  }
}
