// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { valiadateExpression } from '@microsoft/vcpkg-ce/dist/util/safeEval';
import { strictEqual } from 'assert';
import { SuiteLocal } from './SuiteLocal';

describe('Expressions', () => {
  const local = new SuiteLocal();
  const session = local.session;

  after(local.after.bind(local));

  it('Testing the expression parser', () => {
    strictEqual(valiadateExpression(' 1 = 2'), false, 'Assignments not supported');
    strictEqual(valiadateExpression(' 1 == 2'), true, 'Equality supported, numeric literals');
    strictEqual(valiadateExpression(' 1 === 2'), true, 'Strict equality supported');
    strictEqual(valiadateExpression(' $1 === $2'), true, 'Supports variables');
    strictEqual(valiadateExpression(' "abc" === $2'), true, 'Supports strings literals');
    strictEqual(valiadateExpression(' $1 === true'), true, 'Supports boolean literals');
    strictEqual(valiadateExpression(' true'), true, 'literals value');
    strictEqual(valiadateExpression(' true =='), false, 'partial expressions');
  });
});