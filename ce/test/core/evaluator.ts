// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Activation } from '@microsoft/vcpkg-ce/dist/artifacts/activation';
import { Evaluator } from '@microsoft/vcpkg-ce/dist/util/evaluator';
import { linq } from '@microsoft/vcpkg-ce/dist/util/linq';
import { equalsIgnoreCase } from '@microsoft/vcpkg-ce/dist/util/text';
import { strict, strictEqual } from 'assert';
import { SuiteLocal } from './SuiteLocal';

describe('Evaluator', () => {
  const local = new SuiteLocal();
  const fs = local.fs;
  const session = local.session;

  after(local.after.bind(local));

  it('evaluates', () => {
    const activation = new Activation(session);
    activation.environment.set('foo', ['bar']);
    const e = new Evaluator({ $0: 'c:/foo/bar/python.exe' }, process.env, activation.output);

    // handle expressions that use the artifact data
    strictEqual(e.evaluate('$0'), 'c:/foo/bar/python.exe', 'Should return $0 from artifact data');

    // unmatched variables should be passed thru
    strictEqual(e.evaluate('$1'), '$1', 'items with no value are not replaced');

    // handle expressions that use the environment
    const pathVar = linq.keys(process.env).first(each => equalsIgnoreCase(each, 'path'));
    strict(pathVar);
    strictEqual(e.evaluate(`$host.${pathVar}`), process.env[pathVar], 'Should be able to get environment variables from host');

    // handle expressions that use the activation's output
    strictEqual(e.evaluate('$environment.foo'), 'bar', 'Should be able to get environment variables from activation');
  });
});