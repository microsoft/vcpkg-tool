// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { sanitizeUri } from '../../artifacts/artifact';
import { i } from '../../i18n';
import { Session } from '../../session';
import { UpdateCommand } from '../commands/update';
import { Switch } from '../switch';

export class Registry extends Switch {
  switch = 'registry';
  get help() {
    return [
      i`override the path to the registry`
    ];
  }

  async loadRegistries(session: Session, more: Array<string> = []) {
    for (const registry of new Set([...this.values, ...more].map(each => sanitizeUri(each)))) {
      if (registry) {
        const uri = session.parseUri(registry);
        if (await session.isLocalRegistry(uri) || await session.isRemoteRegistry(uri)) {

          const r = session.loadRegistry(uri, 'artifact');
          if (r) {
            try {
              await r.load();
            } catch (e) {
              // try to update the repo
              if (!await UpdateCommand.update(r)) {
                session.channels.error(i`failed to load registry ${uri.toString()}`);
                continue;
              }
            }
            // registry is loaded
            // it should be added to the aggregator
            session.defaultRegistry.add(r, registry);
          }
          continue;
        }
        session.channels.error(i`Invalid registry ${registry}`);
      }
    }

    return session.defaultRegistry;
  }

}