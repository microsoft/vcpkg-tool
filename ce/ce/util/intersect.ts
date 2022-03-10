// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/**
 * Creates an intersection object from two source objects.
 *
 * Typescript nicely supports defining intersection types (ie, Foo & Bar )
 * But if you have two seperate *instances*, and you want to use them as the implementation
 * of that intersection, the language doesn't solve that for you.
 *
 * This function creates a strongly typed proxy type around the two objects,
 * and returns members for the intersection of them.
 *
 * This works well for properties and member functions the same.
 *
 * Members in the primary object will take precedence over members in the secondary object if names conflict.
 *
 * This can also be used to "add" arbitrary members to an existing type (without mutating the original object)
 *
 * @example
 * const combined = intersect( new Foo(), { test: () => { console.debug('testing'); } });
 * combined.test(); // writes out 'testing' to console
 *
 * @param primary primary object - members from this will have precedence.
 * @param secondary secondary object - members from this will be used if primary does not have a member
 */
// eslint-disable-next-line @typescript-eslint/ban-types
export function intersect<T extends object, T2 extends object>(primary: T, secondary: T2, filters = ['constructor']): T & T2 {
  // eslint-disable-next-line keyword-spacing
  return <T & T2><any>new Proxy({ primary, secondary }, <any>{
    // member get proxy handler
    get(target: { primary: T, secondary: T2 }, property: string | symbol, receiver: any) {
      // check for properties on the objects first
      const propertyName = property.toString();

      // provide custom JON impl.
      if (propertyName === 'toJSON') {
        return () => {
          const allKeys = this.ownKeys();
          const o = <any>{};
          for (const i of allKeys) {
            const v = this.get(target, i);
            if (typeof v !== 'function') {
              o[i] = v;
            }
          }
          return o;
        };
      }

      const pv = (<any>target.primary)[property];
      const sv = (<any>target.secondary)[property];

      if (pv !== undefined) {
        if (typeof pv === 'function') {
          return pv.bind(primary);
        }
        return pv;
      }

      if (sv !== undefined) {
        if (typeof sv === 'function') {
          return sv.bind(secondary);
        }
        return sv;
      }

      return undefined;
    },

    // member set proxy handler
    set(target: { primary: T, secondary: T2 }, property: string | symbol, value: any) {
      const propertyName = property.toString();

      if (Object.getOwnPropertyNames(target.primary).indexOf(propertyName) > -1) {
        return (<any>target.primary)[property] = value;
      }
      if (Object.getOwnPropertyNames(target.secondary).indexOf(propertyName) > -1) {
        return (<any>target.secondary)[property] = value;
      }
      return undefined;
    },
    ownKeys(target: { primary: T, secondary: T2 }): ArrayLike<string | symbol> {
      return [...new Set([
        ...Object.getOwnPropertyNames(Object.getPrototypeOf(primary)),
        ...Object.getOwnPropertyNames(primary),
        ...Object.getOwnPropertyNames(Object.getPrototypeOf(secondary)),
        ...Object.getOwnPropertyNames(secondary)].filter(each => filters.indexOf(each) === -1))];
    }

  });
}
