// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { countWhere } from '../../util/linq';
import { installArtifacts, selectArtifacts, showArtifacts } from '../artifacts';
import { Command } from '../command';
import { blank } from '../constants';
import { cmdSwitch } from '../format';
import { debug, error, log, warning } from '../styling';
import { Registry } from '../switches/registry';
import { Version } from '../switches/version';
import { WhatIf } from '../switches/whatIf';

export class AcquireCommand extends Command {
  readonly command = 'acquire';
  readonly aliases = ['install'];
  seeAlso = [];
  argumentsHelp = [];
  version = new Version(this);
  whatIf = new WhatIf(this);
  registrySwitch = new Registry(this);

  get summary() {
    return i`Acquire artifacts in the registry`;
  }

  get description() {
    return [
      i`This allows the consumer to acquire (download and unpack) artifacts. Artifacts must be activated to be used`,
    ];
  }

  override async run() {
    if (this.inputs.length === 0) {
      error(i`No artifacts specified`);
      return false;
    }

    const registries = await this.registrySwitch.loadRegistries(session);

    const versions = this.version.values;
    if (versions.length && this.inputs.length !== versions.length) {
      error(i`Multiple packages specified, but not an equal number of ${cmdSwitch('version')} switches.`);
      return false;
    }

    const artifacts = await selectArtifacts(new Map(this.inputs.map((v, i) => [v, versions[i] || '*'])), registries);

    if (!artifacts) {
      debug('No artifacts selected - stopping');
      return false;
    }

    if (!await showArtifacts(artifacts.artifacts, this.commandLine)) {
      warning(i`No artifacts are acquired`);
      return false;
    }

    const numberOfArtifacts = await countWhere(artifacts.artifacts, async (artifact) => !(!this.commandLine.force && await artifact.isInstalled));

    if (!numberOfArtifacts) {
      log(blank);
      log(i`All artifacts are already installed`);
      return true;
    }

    debug(`Installing ${numberOfArtifacts} artifacts`);

    const [success] = await installArtifacts(session, artifacts.artifacts, { force: this.commandLine.force, language: this.commandLine.language, allLanguages: this.commandLine.allLanguages });

    if (success) {
      log(blank);
      log(i`${numberOfArtifacts} artifacts installed successfuly`);
      return true;
    }

    log(blank);
    log(i`Installation failed -- stopping`);

    return false;
  }
}
