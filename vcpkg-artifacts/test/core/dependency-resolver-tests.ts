// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { selectArtifacts } from '../../cli/artifacts';
import { RegistryDatabase, RegistryResolver } from '../../registries/registries';
import { strict } from 'assert';
import { SuiteLocal } from './SuiteLocal';

describe('Dependency resolver', () => {
  const local = new SuiteLocal();

  after(local.after.bind(local));

  it('Topologically sorts', async () => {
    const db = new RegistryDatabase();
    const localRegistryUri = local.resourcesFolderUri.join('topo-sort-registry');
    const localRegistryStr = localRegistryUri.toString();
    await db.loadRegistry(local.session, localRegistryUri);
    const registryContext = new RegistryResolver(db);
    registryContext.add(localRegistryUri, 'topo');

    const resolved = await selectArtifacts(local.session, new Map<string, string>([['alpha', '*'], ['foxtrot', '1.0.0'], ['delta', '1.0']]), registryContext, 2);
    strict.ok(resolved);
    // beta and echo being transposed would also be a correct order.
    // alpha and foxtrot being transposed would also be a correct order.
    strict.deepStrictEqual(resolved.map(a => [a.uniqueId, a.initialSelection, a.depth, a.requestedVersion]), [
      [localRegistryStr + '::delta::1.0.0', true, 4, '1.0'],
      [localRegistryStr + '::charlie::1.0.0', false, 3, undefined],
      [localRegistryStr + '::bravo::1.0.0', false, 2, undefined],
      [localRegistryStr + '::echo::1.0.0', false, 2, undefined],
      [localRegistryStr + '::alpha::1.0.0', true, 1, '*'],
      [localRegistryStr + '::foxtrot::1.0.0', true, 1, '1.0.0']
    ]);
  });
});
