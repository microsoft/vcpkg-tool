To validate schemas, you can use the ajv validator.

```
npm install -g ajv
npm install -g ajv-formats
```

For example,

```
ajv validate -c ajv-formats -s .\vcpkg.schema.json -r .\vcpkg-schema-definitions.schema.json -d C:\Dev\vcpkg\ports\**\vcpkg.json --all-errors
```
