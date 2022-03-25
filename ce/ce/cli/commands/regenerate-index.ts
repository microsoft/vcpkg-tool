// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Registry } from '../../artifacts/registry';
import { registryIndexFile } from '../../constants';
import { i } from '../../i18n';
import { session } from '../../main';
import { Registries } from '../../registries/registries';
import { Uri } from '../../util/uri';
import { Command } from '../command';
import { cli } from '../constants';
import { error, log, writeException } from '../styling';
import { Project } from '../switches/project';
import { Registry as RegSwitch } from '../switches/registry';
import { WhatIf } from '../switches/whatIf';

export class RegenerateCommand extends Command {
  readonly command = 'regenerate';
  project = new Project(this);
  readonly aliases = ['regen'];
  readonly regSwitch = new RegSwitch(this, { required: true });
  seeAlso = [];
  argumentsHelp = [];

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
    let registries: Registries = await this.regSwitch.loadRegistries(session);
    registries = (await this.project.manifest)?.registries ?? registries;

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
            session.loadRegistry(location, 'artifact') :  // a folder
            registries.getRegistry(registryNameOrLocation); // a registry name or other location.
        }
        if (registry) {
          if (Uri.isInvalid(registry.location)) {
            error(i`Registry: '${registryNameOrLocation}' does not have an index to regenerate.`);
            return false;
          }
          log(i`Regenerating index for ${registry.location.formatted}`);
          await registry.regenerate();
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