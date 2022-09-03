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
  id = randomWords(idwords).join('/');
  version = rndSemver();
  summary = sentence();
  description = paragraph();
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
        p.version = rndSemver();
        const target = repoFolder.join(`${p.id}-${p.version}.yaml`);
        await target.writeFile(Buffer.from(serialize(p), 'utf8'));
      }
    }
    // now copy the files from the test folder
    await local.fs.copy(local.rootFolderUri.join('resources', 'repo'), repoFolder);
  });
});
