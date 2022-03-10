// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { serialize } from '@microsoft/vcpkg-ce/dist/yaml/yaml';
import { createHash } from 'crypto';
import { describe } from 'mocha';
import { getNouns, paragraph, sentence } from 'txtgen';
import { SuiteLocal } from './SuiteLocal';


const idwords = ['compilers', 'tools', 'contoso', 'adatum', 'cheese', 'utility', 'crunch', 'make', 'build', 'cc', 'c++', 'debugger', 'flasher', 'whatever', 'else'];
const firstNames = ['sandy', 'tracy', 'skyler', 'nick', 'bob', 'jack', 'alice'];
const lastNames = ['smith', 'jones', 'quack', 'fry', 'lunch'];
const companies = ['contoso', 'adatum', 'etcetcetc'];
const roles = ['pusher', 'do-er', 'maker', 'publisher', 'slacker', 'originator'];
const suffixes = ['com', 'org', 'net', 'gov', 'ca', 'uk', 'etc'];

function rnd(min: number, max: number) {
  return Math.floor((Math.random() * (max - min)) + min);
}

function rndSemver() {
  return [rnd(0, 100), rnd(0, 200), rnd(0, 5000)].join('.');
}

function randomWord(from: Array<string>) {
  return from[rnd(0, from.length - 1)];
}

function randomHost() {
  return `${randomWord(companies)}.${randomWord(suffixes)}`;
}

function randomContacts() {
  const result = <any>{};

  for (let i = 0; i < rnd(0, 3); i++) {
    const name = `${randomWord(firstNames)} ${randomWord(lastNames)}`;
    result[name] = {
      email: `${randomWord(getNouns())}@${randomHost()}`,
      roles: randomWords(roles, 1, 3)
    };
  }
  return result;
}

function randomWords(from: Array<string>, min = 3, max = 6) {
  const s = new Set<string>();
  const n = rnd(min, max);
  while (s.size < n) {
    s.add(randomWord(from));
  }
  return [...s.values()];
}

class Template {
  info = {
    id: randomWords(idwords).join('/'),
    version: rndSemver(),
    summary: sentence(),
    description: paragraph(),
  };

  contacts = randomContacts();
  install = {
    unzip: `https://${randomHost()}/${sentence().replace(/ /g, '/')}.zip`,
    sha256: createHash('sha256').update(sentence()).digest('hex'),
  };
}

describe('StandardRegistry Tests', () => {

  const local = new SuiteLocal();

  after(local.after.bind(local));

  before(async () => {
    const repoFolder = local.session.homeFolder.join('repo', 'default');
    // creates a bunch of artifacts, with multiple versions
    const pkgs = 100;

    for (let i = 0; i < pkgs; i++) {
      const versions = rnd(1, 5);
      const t = new Template();
      for (let j = 0; j < versions; j++) {
        const p = {
          ...t,
        };
        p.info.version = rndSemver();
        const target = repoFolder.join(`${p.info.id}-${p.info.version}.yaml`);
        await target.writeFile(Buffer.from(serialize(p), 'utf8'));
      }
    }
    // now copy the files from the test folder
    await local.fs.copy(local.rootFolderUri.join('resources', 'repo'), repoFolder);
  });

  /* fixme!
  it('can save and load the index', async () => {
    const registry = local.session.defaultRegistry;
    await registry.regenerate();
    await registry.save();

    const anotherregistry = new LocalRegistry(local.session, local.session.homeFolder.join('repo', 'default'));
    await anotherregistry.load();
    strict.equal(registry.count, anotherregistry.count, 'repo should be the same size as the last one');
  });

  it('Loads a bunch items', async () => {
    const registry = local.session.defaultRegistry;
    await registry.regenerate();

    const all = await registry.openArtifacts(registry.values);
    const items = [...all.values()].flat();
    strict.equal(items.length, registry.count, 'Should have loaded everything');

  });


  it('Create index from some data', async () => {
    const start = process.uptime() * 1000;

    const registry = local.session.defaultRegistry;
    local.session.channels.on('debug', (d, x, m) => SuiteSuiteLocal.log(`${m}msec : ${d}`));
    await registry.regenerate();
    await registry.save();

    const arm = registry.where.id.equals('compilers/gnu/gcc/arm-none-eabi').items;
    strict.equal(arm.length, 3, 'should be 3 results');

    local.session.channels.on('debug', (t) => SuiteSuiteLocal.log(t));

    const map = await registry.openArtifacts(arm);
    strict.equal(map.size, 1, 'Should have one pkg id');

    const versions = map.get('compilers/gnu/gcc/arm-none-eabi');
    strict.ok(versions, 'should have some versions');
    strict.equal(versions.length, 3, 'should have three versions of the package');

    const anotherregistry = new LocalRegistry(local.session, local.session.homeFolder.join('repo', 'default'));
    await anotherregistry.load();
    const anotherArm = registry.where.id.equals('compilers/gnu/gcc/arm-none-eabi').items;
    strict.equal(anotherArm.length, 3, 'should be 3 results');


    const cmakes = registry.where.id.equals('tools/kitware/cmake').items;
    strict.equal(cmakes.length, 5, 'should be 5 results');
  });
  */
});