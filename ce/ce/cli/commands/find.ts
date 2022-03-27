// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import { i } from '../../i18n';
import { session } from '../../main';
import { Registries } from '../../registries/registries';
import { Command } from '../command';
import { artifactIdentity } from '../format';
import { Table } from '../markdown-table';
import { debug, log } from '../styling';
import { Project } from '../switches/project';
import { Registry } from '../switches/registry';
import { Version } from '../switches/version';

export class FindCommand extends Command {
  readonly command = 'find';
  readonly aliases = ['search'];
  seeAlso = [];
  argumentsHelp = [];

  version = new Version(this);
  registrySwitch = new Registry(this);
  project = new Project(this);

  get summary() {
    return i`Find artifacts in the registry`;
  }

  get description() {
    return [
      i`This allows the user to find artifacts based on some criteria.`,
    ];
  }

  override async run() {
    // load registries (from the current project too if available)
    let registries: Registries = await this.registrySwitch.loadRegistries(session);
    registries = (await this.project.manifest)?.registries ?? registries;

    debug(`using registries: ${[...registries].map(([registry, registryNames]) => registryNames[0]).join(', ')}`);
    const table = new Table('Artifact', 'Version', 'Summary');

    for (const each of this.inputs) {
      for (const [registry, id, artifacts] of await registries.search({ idOrShortName: each, version: this.version.value })) {
        const latest = artifacts[0];
        if (!latest.metadata.info.dependencyOnly) {
          const name = artifactIdentity(latest.registryId, id, latest.shortName);
          table.push(name, latest.metadata.info.version, latest.metadata.info.summary || '');
        }
      }
    }

    log(table.toString());
    log();
    return true;
  }
}