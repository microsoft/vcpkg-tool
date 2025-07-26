// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { Artifact } from '../../artifacts/artifact';
import { Registry, RegistryDatabase, RegistryResolver, SearchCriteria } from '../../registries/registries';
import { Uri } from '../../util/uri';
import { SuiteLocal } from './SuiteLocal';

class FakeRegistry implements Registry {
  constructor(public readonly location: Uri) {
  }

  get count() { return 1; }

  async search(_criteria?: SearchCriteria): Promise<Array<[string, Array<Artifact>]>> {
    throw new Error('not implemented');
  }

  load(_force?: boolean): Promise<void> { return Promise.resolve(); }
  save(): Promise<void> { return Promise.resolve(); }
  update(_displayName?: string): Promise<void> { return Promise.resolve(); }
  regenerate(_normalize?: boolean): Promise<void> { return Promise.resolve(); }
}

describe('Registry resolver', () => {
  const local = new SuiteLocal();

  after(local.after.bind(local));

  const appleUri = local.fs.parseUri('https://example.com/apple.zip');
  const appleRegistry = new FakeRegistry(appleUri);
  const bananaUri = local.fs.parseUri('https://example.com/banana.zip');
  const bananaRegistry = new FakeRegistry(appleUri);
  const cherryUri = local.fs.parseUri('https://example.com/cherry.zip');
  const cherryRegistry = new FakeRegistry(appleUri);
  const alphaUri = local.fs.parseUri('https://example.com/alpha.zip');
  const alphaRegistry = new FakeRegistry(alphaUri);
  const andromedaUri = local.fs.parseUri('https://example.com/andromeda.zip');
  const andromedaRegistry = new FakeRegistry(alphaUri);

  const db = new RegistryDatabase();
  db.add(appleUri, appleRegistry);
  db.add(bananaUri, bananaRegistry);
  db.add(cherryUri, cherryRegistry);
  db.add(alphaUri, alphaRegistry);
  db.add(andromedaUri, andromedaRegistry);

  const globalContext = new RegistryResolver(db);
  globalContext.add(appleUri, 'a');
  globalContext.add(bananaUri, 'b');
  globalContext.add(alphaUri, 'apple');

  const projectContext = new RegistryResolver(db);
  projectContext.add(appleUri, 'apple');
  projectContext.add(cherryUri, 'cherry');

  const combined = globalContext.with(projectContext);

  it('Knows names in the project', () => {
    strict.equal(combined.getRegistryByName('apple'), appleRegistry);
    strict.equal(combined.getRegistryByName('cherry'), cherryRegistry);
  });

  it('Projects do not know different names from the same URI from the global context', () => {
    strict.equal(projectContext.getRegistryByName('a'), undefined);
    strict.equal(projectContext.getRegistryByName('b'), undefined);
  });

  it('Knows only URIs from either context', () => {
    strict.equal(combined.getRegistryByUri(appleUri), appleRegistry);
    strict.equal(combined.getRegistryByUri(bananaUri), bananaRegistry);
    strict.equal(combined.getRegistryByUri(cherryUri), cherryRegistry);
    strict.equal(combined.getRegistryByUri(alphaUri), alphaRegistry);
    strict.equal(combined.getRegistryByUri(andromedaUri), undefined); // database knows but context doesn't
  });

  it('Chooses names of identical URIs from the project', () => {
    strict.equal(combined.getRegistryName(appleUri), 'apple');
    strict.equal(combined.getRegistryDisplayName(appleUri), 'apple');
    strict.equal(combined.getRegistryName(cherryUri), 'cherry');
    strict.equal(combined.getRegistryDisplayName(cherryUri), 'cherry');
  });

  it('Chooses names not in the project from the global configuration', () => {
    strict.equal(combined.getRegistryName(bananaUri), 'b'); // not in project, so global name is used
    strict.equal(combined.getRegistryDisplayName(bananaUri), 'b');
  });

  it('Does not know names with different meaning in the project', () => {
    // Global called this 'apple', but project called 'apple' appleUri, so it can only be displayed as
    // the full URI (in []s)
    strict.equal(combined.getRegistryName(alphaUri), undefined); // not in project, so global name is used
    strict.equal(combined.getRegistryDisplayName(alphaUri), '[https://example.com/alpha.zip]');
  });
});
