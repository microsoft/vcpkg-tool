// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import * as vm from 'vm';

export function safeEval(code: string, context?: any): any {
  const sandbox = vm.createContext(context);
  const response = 'SAFE_EVAL_' + Math.floor(Math.random() * 1000000);
  sandbox[response] = {};
  vm.runInContext(`try {  ${response} = ${code} } catch (e) { ${response} = undefined }`, sandbox);
  const result = sandbox[response];
  delete sandbox[response];
  return result;
}