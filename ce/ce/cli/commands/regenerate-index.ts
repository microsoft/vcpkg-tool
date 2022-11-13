// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { resolve } from 'path';
import { registryIndexFile } from '../../constants';
import { i } from '../../i18n';
import { session } from '../../main';
import { LocalRegistry } from '../../registries/LocalRegistry';
import { Command } from '../command';
import { cli } from '../constants';
import { error, log } from '../styling';
import { Normalize } from '../switches/normalize';

export class RegenerateCommand extends Command {
  readonly command = 'regenerate';
  readonly aliases = ['regen'];
  readonly normalize = new Normalize(this);
  seeAlso = [];
  argumentsHelp = [];

  get summary() {
    return i`regenerate the index for a registry`;
  }

  get description() {
    return [
      i`This allows the user to regenerate the ${registryIndexFile} files for a ${cli} registry.`,
    ];
  }

  override async run() {
    for (const input of this.inputs) {
      const inputUri = session.fileSystem.file(resolve(input));
      const localReg = new LocalRegistry(session, inputUri);
      try {
        await localReg.load();
        log(i`Regenerating index for ${input}`);
        await localReg.regenerate(this.normalize.active);
        const count = localReg.count;
        if (count) {
          await localReg.save();
          log(i`Regeneration complete. Index contains ${count} metadata files`);
        } else {
          // looks like  the registry contained no items
          error(i`Registry: '${input}' contains no artifacts.`);
        }
      } catch (e) {
        let message = 'unknown';
        if (e instanceof Error) {
          message = e.message;
        }

        log(i`error ${input}: ` + message);
        return false;
      }
    }

    return true;
  }
}
