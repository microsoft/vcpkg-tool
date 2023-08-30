// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Artifact, buildRegistryResolver } from '../../artifacts/artifact';
import { i } from '../../i18n';
import { session } from '../../main';
import { countWhere } from '../../util/linq';
import { acquireArtifacts, selectArtifacts, showArtifacts } from '../artifacts';
import { Command } from '../command';
import { cmdSwitch } from '../format';
import { debug, error, log, warning } from '../styling';
import { Project } from '../switches/project';
import { Version } from '../switches/version';

export class AcquireCommand extends Command {
  readonly command = 'acquire';
  version: Version = new Version(this);
  project: Project = new Project(this);

  override async run() {
    if (this.inputs.length === 0) {
      error(i`No artifacts specified`);
      return false;
    }

    const versions = this.version.values;
    if (versions.length && this.inputs.length !== versions.length) {
      error(`Multiple packages specified, but not an equal number of ${cmdSwitch('version')} switches`);
      return false;
    }

    const resolver = session.globalRegistryResolver.with(
      await buildRegistryResolver(session, (await this.project.manifest)?.metadata.registries));
    const resolved = await selectArtifacts(session, new Map(this.inputs.map((v, i) => [v, versions[i] || '*'])), resolver, 2);
    if (!resolved) {
      debug('No artifacts selected - stopping');
      return false;
    }

    if (!await showArtifacts(resolved, resolver, this.commandLine)) {
      warning(i`No artifacts are acquired`);
      return false;
    }

    const numberOfArtifacts = await countWhere(resolved, async (resolution) => {
      const artifact = resolution.artifact;
      return !(!this.commandLine.force && artifact instanceof Artifact && await artifact.isInstalled);
    });

    if (!numberOfArtifacts) {
      log(i`All artifacts are already installed`);
      return true;
    }

    debug(`Installing ${numberOfArtifacts} artifacts`);
    const success = await acquireArtifacts(session, resolved, resolver, { force: this.commandLine.force, language: this.commandLine.language, allLanguages: this.commandLine.allLanguages });
    if (success) {
      log(i`${numberOfArtifacts} artifacts installed successfully`);
    } else {
      log(i`Installation failed -- stopping`);
    }

    return success;
  }
}
