> **Breadcrumb:** [06 - vct Design](../06-vct.md) → §5. Automation vs Control: An Example

## 5. Automation vs Control: An Example

Three usage patterns for `vct install firefox`:

### 5.1 Automation (Convenience, the Default)

```
$ vct install firefox
Resolving dependencies... done
Generating component configuration... done
Installing... done

Installed package: firefox 118.0
Components added: firefox, nss_drv
Launcher entry registered: Internet -> Firefox
```

### 5.2 Preview (Transparency)

```
$ vct install firefox --explain
The following steps are planned:

1. Fetch package metadata
   - firefox 118.0
   - Dependencies: nss_drv, libnss

2. Add to component tree
   Under init:
     - firefox (requires Gui, Input, File_system sessions)
     - nss_drv (already present, reused)

3. Configure session routing
   firefox.Gui      -> nitpicker
   firefox.Input    -> input_drv
   firefox.File_system -> vfs (read-only: /app/firefox)

4. Register launcher entry
   Category: Internet

Re-run without --explain to execute.
```

### 5.3 Manual Steps (Control)

```
$ vct install firefox --manual
1/4 Fetch package metadata. [Y/n] y
   firefox 118.0, dependencies: nss_drv, libnss

2/4 Install dependencies. [Y/n] y
   nss_drv: already installed
   libnss: installing... done

3/4 Modify the component tree.
   The following node will be added under init:
     - firefox

   Continue? [Y/n] y

4/4 Configure session routing.
   Applying default routing (override with `vct component route firefox`).

Done.
```

All three patterns use the same backend service (`sponge_pkgd`); only
the level of automation differs. That is how the principle "automation
is the default, control is the door" is realized.

---

