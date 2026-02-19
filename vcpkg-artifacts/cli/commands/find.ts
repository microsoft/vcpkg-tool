// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import chalk from 'chalk';
import { buildRegistryResolver } from '../../artifacts/artifact';
import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { Table } from '../console-table';
import { error, log } from '../styling';
import { Project } from '../switches/project';
import { Version } from '../switches/version';

export class FindCommand extends Command {
  readonly command = 'find';

  version = new Version(this);
  project = new Project(this);

  override async run() {
    // load registries (from the current project too if available)
    const resolver = session.globalRegistryResolver.with(
      await buildRegistryResolver(session, (await this.project.manifest)?.metadata.registries));
    const table = new Table(i`Artifact`, i`Version`, i`Summary`);

    let anyEntries = false;
    for (const each of this.inputs) {
      const hasColon = each.indexOf(':') > -1;
      // eslint-disable-next-line prefer-const
      for (let [display, artifactVersions] of await resolver.search({
        // use keyword search if no registry is specified
        keyword: hasColon ? undefined : each,
        // otherwise use the criteria as an id
        idOrShortName: hasColon ? each : undefined,
        version: this.version.value
      })) {
        if (!this.version.isRangeOfVersions) {
          // if the user didn't specify a range, just show the latest version that was returned
          artifactVersions.splice(1);
        }
        for (const result of artifactVersions) {
          if (!result.metadata.dependencyOnly) {
            anyEntries = true;
            table.push(display, result.metadata.version, result.metadata.summary || '');
          }
        }
      }
    }

    if (!anyEntries) {
      error(i`No artifacts found matching criteria: ${chalk.cyan.bold(this.inputs.join(', '))}`);
      return false;
    }

    log(table.toString());
    log();
    return true;
  }
}
