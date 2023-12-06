// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { buildRegistryResolver } from '../../artifacts/artifact';
import { i } from '../../i18n';
import { session } from '../../main';
import { selectArtifacts, showArtifacts } from '../artifacts';
import { Command } from '../command';
import { cmdSwitch } from '../format';
import { activate } from '../project';
import { error, warning } from '../styling';
import { MSBuildProps } from '../switches/msbuild-props';
import { Project } from '../switches/project';
import { Version } from '../switches/version';

export class UseCommand extends Command {
  readonly command = 'use';
  version = new Version(this);
  project = new Project(this);
  msbuildProps = new MSBuildProps(this);

  override async run() : Promise<boolean> {
    if (this.inputs.length === 0) {
      error(i`No artifacts specified`);
      return false;
    }

    const resolver = session.globalRegistryResolver.with(
      await buildRegistryResolver(session, (await this.project.manifest)?.metadata.registries));
    const versions = this.version.values;
    if (versions.length && this.inputs.length !== versions.length) {
      error(`Multiple packages specified, but not an equal number of ${cmdSwitch('version')} switches`);
      return false;
    }

    const selections = new Map(this.inputs.map((v, i) => [v, versions[i] || '*']));
    const artifacts = await selectArtifacts(session, selections, resolver, 2);
    if (!artifacts) {
      return false;
    }

    if (!await showArtifacts(artifacts, resolver, this.commandLine)) {
      warning(i`No artifacts are being acquired`);
      return false;
    }

    return activate(session, true, this.inputs, artifacts, resolver,
      { force: this.commandLine.force, language: this.commandLine.language, allLanguages: this.commandLine.allLanguages });
  }
}
