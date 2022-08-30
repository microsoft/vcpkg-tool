// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Registry } from '../../artifacts/registry';
import { registryIndexFile } from '../../constants';
import { i } from '../../i18n';
import { session } from '../../main';
import { Uri } from '../../util/uri';
import { Command } from '../command';
import { cli } from '../constants';
import { error, log, writeException } from '../styling';
import { Normalize } from '../switches/normalize';
import { Project } from '../switches/project';
import { WhatIf } from '../switches/whatIf';

export class RegenerateCommand extends Command {
  readonly command = 'regenerate';
  readonly aliases = ['regen'];
  readonly normalize = new Normalize(this);
  seeAlso = [];
  argumentsHelp = [];
  project = new Project(this);
  whatIf = new WhatIf(this);

  get summary() {
    return i`regenerate the index for a registry`;
  }

  get description() {
    return [
      i`This allows the user to regenerate the ${registryIndexFile} files for a ${cli} registry.`,
    ];
  }

  override async run() {
    const registries = session.loadDefaultRegistryContext(await this.project.manifest);
    for (const registryNameOrLocation of this.inputs) {
      let registry: Registry | undefined;
      try {
        if (registries.has(registryNameOrLocation)) {
          // check for named registries first.
          registry = registries.getRegistry(registryNameOrLocation);
          await registry?.load();
        } else {
          // see if the name is a location
          const location = await session.parseLocation(registryNameOrLocation);
          registry = location ?
            await session.loadRegistry(location) :  // a folder
            registries.getRegistry(registryNameOrLocation); // a registry name or other location.
        }
        if (registry) {
          if (Uri.isInvalid(registry.location)) {
            error(i`Registry: '${registryNameOrLocation}' does not have an index to regenerate.`);
            return false;
          }
          log(i`Regenerating index for ${registry.location.formatted}`);
          await registry.regenerate(!!this.normalize);
          if (registry.count) {
            await registry.save();
            log(i`Regeneration complete. Index contains ${registry.count} metadata files`);
            continue;
          }
          // looks like  the registry contained no items
          error(i`Registry: '${registry.location.formatted}' contains no artifacts.`);
          continue;
        }

        error(i`Unrecognized registry: ${registryNameOrLocation}`);
        return false;

      } catch (e) {
        log(i`Regeneration failed for ${registryNameOrLocation.toString()}`);
        writeException(e);
        return false;
      }
    }
    return true;
  }
}
