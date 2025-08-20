// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { resolve } from 'path';
import { i } from '../../i18n';
import { session } from '../../main';
import { LocalRegistry } from '../../registries/LocalRegistry';
import { Command } from '../command';
import { error, log } from '../styling';
import { Normalize } from '../switches/normalize';

export class RegenerateCommand extends Command {
  readonly command = 'regenerate';
  readonly normalize = new Normalize(this);

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
