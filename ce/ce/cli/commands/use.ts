// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Registries } from '../../registries/registries';
import { installArtifacts, selectArtifacts, showArtifacts } from '../artifacts';
import { Command } from '../command';
import { cmdSwitch } from '../format';
import { error, log, warning } from '../styling';
import { MSBuildProps } from '../switches/msbuild-props';
import { Project } from '../switches/project';
import { Registry } from '../switches/registry';
import { Version } from '../switches/version';
import { WhatIf } from '../switches/whatIf';

export class UseCommand extends Command {
  readonly command = 'use';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  version = new Version(this);
  whatIf = new WhatIf(this);
  registrySwitch = new Registry(this);
  project = new Project(this);
  msbuildProps = new MSBuildProps(this);

  get summary() {
    return i`Instantly activates an artifact outside of the project`;
  }

  get description() {
    return [
      i`This will instantly activate an artifact .`,
    ];
  }

  override async run() {
    if (this.inputs.length === 0) {
      error(i`No artifacts specified`);
      return false;
    }

    // load registries (from the current project too if available)
    let registries: Registries = await this.registrySwitch.loadRegistries(session);
    registries = (await this.project.manifest)?.registries ?? registries;

    const versions = this.version.values;
    if (versions.length && this.inputs.length !== versions.length) {
      error(i`Multiple packages specified, but not an equal number of ${cmdSwitch('version')} switches`);
      return false;
    }

    const selections = new Map(this.inputs.map((v, i) => [v, versions[i] || '*']));
    const artifacts = await selectArtifacts(selections, registries);

    if (!artifacts) {
      return false;
    }

    if (!await showArtifacts(artifacts.artifacts, this.commandLine)) {
      warning(i`No artifacts are being acquired`);
      return false;
    }

    const [success, artifactStatus, activation] = await installArtifacts(session, artifacts.artifacts, { force: this.commandLine.force, language: this.commandLine.language, allLanguages: this.commandLine.allLanguages });
    if (success) {
      log(i`Activating individual artifacts`);
      await session.setActivationInPostscript(activation, false);
      const propsFile = this.msbuildProps.value;
      if (propsFile) {
        await propsFile.writeUTF8(activation.generateMSBuild(artifactStatus.keys()));
      }
    } else {
      return false;
    }
    return true;
  }
}