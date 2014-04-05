<?php

/**
 * @file
 * Bandaid for Drush.
 *
 * Warning: This is still much a proof of concept, with duplicate code, harcoded
 * values, missing documentation and lacking error messages. Those of a sensible
 * dispossition should avert their eyes.
 */

require 'vendor/autoload.php';

use Symfony\Component\Yaml\Yaml;

/**
 * Implements hook_drush_command().
 */
function bandaid_drush_command() {
  $items['bandaid-patch'] = array(
    'description' => 'Add a patch.',
    'arguments' => array(
      'patch' => 'URL of patch to apply.',
      'project' => 'Project to patch. If left out, current dir is pactched.',
    ),
    'options' => array(
      'home' => 'URL of issue.',
      'reason' => 'Reason for patch.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );

  $items['bandaid-tearoff'] = array(
    'description' => 'Tear off patches.',
    'arguments' => array(
      'project' => 'Project to remove patches from.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );

  $items['bandaid-apply'] = array(
    'description' => 'Apply patches.',
    'arguments' => array(
      'project' => 'Project to apply patches to.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );

  return $items;
}

function drush_bandaid_tearoff($project) {
  $project_dir = realpath(drush_cwd() . '/' . $project);
  $yaml_file = $project_dir . '.yml';
  $info_file = $project_dir . '/' . $project . '.info';
  $local_patch_file = $project_dir . '.local.patch';

  chdir(dirname($project_dir));
  $project = basename($project_dir);

  if (!file_exists($info_file)) {
    return drush_set_error('NO_INFO_FILE');
  }

  $version_info = _bandaid_parse_info($info_file);
  if (!$version_info) {
    return FALSE;
  }

  if (file_exists($local_patch_file)) {
    drush_log(dt('Local patch file exists, it will be overwritten.'), 'warning');
    if (!drush_confirm('Continue anyway?')) {
      return drush_user_abort();
    }

  }

  if (file_exists($yaml_file)) {
    $yaml = Yaml::parse($yaml_file);
    $patches = $yaml['patches'];
  }

  // Clone repo.
  drush_log(dt('Cloning @name.', array('@name' => $project)), 'status');
  $checkout_name = 'bandaid_workidir';
  $checkout_dir = dirname($project_dir) . '/' . $checkout_name;
  if (!drush_shell_exec_interactive('git clone ssh://git@git.drupal.org/project/' . $version_info['project'] . ' ' . $checkout_name)) {
    return drush_set_error('CANNOT_CLONE');
  }
  drush_register_file_for_deletion(realpath($checkout_name));

  chdir($checkout_name);

  $branch = $version_info['core'] . '-' . $version_info['major'] . '.x';
  $tag = $version_info['core'] . '-' . $version_info['version'];
  $checkout_ref = $version_info['core'] . '-' . $version_info['version'];
  if ($version_info['commits']) {
    // Get history from base version to head.
    if (!drush_shell_exec('git log --format=oneline ' . $tag . '..' . $branch)) {
      drush_print_r(drush_shell_exec_output());
      return drush_set_error('CANNOT_GET_HISTORY');
    }
    // Flip the order so we have the oldest commit first.
    $commits = array_reverse(drush_shell_exec_output());
    // Pick out the nth commit, and split the line.
    list($checkout_ref, $description) = explode(' ', $commits[$version_info['commits'] - 1], 2);
  }

  // Checkout base version.
  drush_log(dt('Checking out @ref.', array('@ref' => $checkout_ref)), 'status');
  if (!empty($description)) {
    drush_log(dt('Commit message: @desc.', array('@desc' => $description)), 'status');
  }
  if (!drush_shell_exec('git checkout ' . $checkout_ref)) {
    return drush_set_error('CANNOT_CHECKOUT_BASE');
  }

  // Create patch branch.
  if (!drush_shell_exec('git branch bandaid_patch')) {
    return drush_set_error('CANNOT_BRANCH');
  }

  // Create local branch.
  if (!drush_shell_exec('git branch bandaid_local')) {
    return drush_set_error('CANNOT_BRANCH');
  }

  // Apply patches to patch branch.
  if (!empty($patches)) {
    if (!drush_shell_exec('git checkout bandaid_patch')) {
      return drush_set_error('CANNOT_CHECKOUT_BRANCH');
    }

    if (!_bandaid_patch($patches)) {
      return;
    }
  }

  // Copy the local project to the local branch.
  if (!drush_shell_exec('git checkout bandaid_local')) {
    return drush_set_error('CANNOT_CHECKOUT_BRANCH');
  }
  // Clear out the files in the repo and copy in the files from the project.
  foreach (drush_scan_directory($checkout_dir, '{.*}', array('.', '..', '.git'), 0, FALSE, 'filename', 0, TRUE) as $file) {
    drush_delete_dir($file->filename);
  }
  drush_copy_dir($project_dir, $checkout_dir, FILE_EXISTS_MERGE);
  // d.o automatically adds the LICENSE.txt, so we remove it now, in order to
  // not polute the diff here, but add it again when creating the new project
  // from git, in order not to polute the diff in the site.
  if (file_exists('LICENSE.txt')) {
    $licence = file_get_contents('LICENSE.txt');
    $licence_perms = fileperms('LICENSE.txt');
    unlink('LICENSE.txt');
  }
  if (!_bandaid_fix_info_files($checkout_dir)) {
    return drush_set_error('INFO_FILE_ERROR');
  }
  if (!drush_shell_exec('git add --all .')) {
    return drush_set_error('CANNOT_ADD');
  }
  if (!drush_shell_exec('git commit -m ' . drush_escapeshellarg("Committing local."))) {
    return drush_set_error('CANNOT_COMMIT');
  }

  // Create local patch file.
  if (!drush_shell_exec('git diff bandaid_patch bandaid_local')) {
    return drush_set_error('CANNOT_DIFF');
  }
  $patch = implode("\n", drush_shell_exec_output());
  if (!empty($patch)) {
    // Due to going through drush_shell_exec_output(), we lost the trailing
    // newline.
    if (!file_put_contents($local_patch_file, $patch . "\n")) {
      return drush_set_error('CANNOT_CREATE_PATCH');
    }
  }

  // Return to a pristine checkout.
  if (!drush_shell_exec('git checkout ' . $checkout_ref)) {
    return drush_set_error('CANNOT_CHECKOUT_BASE');
  }
  // Recreate LICENSE.txt.
  if (!empty($licence)) {
    file_put_contents('LICENSE.txt', $licence);
    chmod('LICENSE.txt', $licence_perms);
  }

  // Fiddle with info files like d.o and drush.
  $version = $version_info['core'] . '-' . $version_info['version'] . (!empty($version_info['commits']) ? '+' . $version_info['commits'] . '-dev' : '');
  _bandaid__inject_info_file_metadata($checkout_dir, $project, $version);

  if (!drush_move_dir($project_dir, $project_dir . '.old')) {
    return FALSE;
  }

  if (!drush_delete_dir('.git')) {
    return drush_set_error('CANNOT_DELETE_GIT');
  }
  chdir(dirname($project_dir));
  if (!drush_move_dir($checkout_dir, $project_dir)) {
    return FALSE;
  }
  drush_register_file_for_deletion($project_dir . '.old');

  /*
   *clone repo.
   * check out version/commit
   * create bandaid_patch branch
   * create bandaid_local branch
   * apply patches from yaml file to patch branch, commit
    clean files from local branch, copy in orginal module
    remove drush git info from info files
    commit
    diff branches, create <project>.local.patch
  */
}

function drush_bandaid_apply($project) {
  $project_dir = realpath(drush_cwd() . '/' . $project);
  $yaml_file = $project_dir . '.yml';
  $info_file = $project_dir . '/' . $project . '.info';
  $local_patch_file = $project_dir . '.local.patch';

  chdir(dirname($project_dir));
  $project = basename($project_dir);

  if (!file_exists($info_file)) {
    return drush_set_error('NO_INFO_FILE');
  }

  $version_info = _bandaid_parse_info($info_file);
  if (!$version_info) {
    return FALSE;
  }

  if (file_exists($yaml_file)) {
    $yaml = Yaml::parse($yaml_file);
    $patches = $yaml['patches'];
  }

  // Clone repo.
  drush_log(dt('Cloning @name.', array('@name' => $project)), 'status');
  $checkout_name = 'bandaid_workidir';
  $checkout_dir = dirname($project_dir) . '/' . $checkout_name;
  if (!drush_shell_exec_interactive('git clone ssh://git@git.drupal.org/project/' . $version_info['project'] . ' ' . $checkout_name)) {
    return drush_set_error('CANNOT_CLONE');
  }
  drush_register_file_for_deletion(realpath($checkout_name));

  chdir($checkout_name);

  $branch = $version_info['core'] . '-' . $version_info['major'] . '.x';
  $tag = $version_info['core'] . '-' . $version_info['version'];
  $checkout_ref = $version_info['core'] . '-' . $version_info['version'];
  if ($version_info['commits']) {
    // Get history from base version to head.
    if (!drush_shell_exec('git log --format=oneline ' . $tag . '..' . $branch)) {
      drush_print_r(drush_shell_exec_output());
      return drush_set_error('CANNOT_GET_HISTORY');
    }
    // Flip the order so we have the oldest commit first.
    $commits = array_reverse(drush_shell_exec_output());
    // Pick out the nth commit, and split the line.
    list($checkout_ref, $description) = explode(' ', $commits[$version_info['commits'] - 1], 2);
  }

  // Checkout base version.
  drush_log(dt('Checking out @ref.', array('@ref' => $checkout_ref)), 'status');
  if (!empty($description)) {
    drush_log(dt('Commit message: @desc.', array('@desc' => $description)), 'status');
  }
  if (!drush_shell_exec('git checkout ' . $checkout_ref)) {
    return drush_set_error('CANNOT_CHECKOUT_BASE');
  }

  // Create local branch.
  if (!drush_shell_exec('git branch bandaid_local')) {
    return drush_set_error('CANNOT_BRANCH');
  }

  // Copy the local project to the local branch.
  if (!drush_shell_exec('git checkout bandaid_local')) {
    return drush_set_error('CANNOT_CHECKOUT_BRANCH');
  }
  // Clear out the files in the repo and copy in the files from the project.
  foreach (drush_scan_directory($checkout_dir, '{.*}', array('.', '..', '.git'), 0, FALSE, 'filename', 0, TRUE) as $file) {
    drush_delete_dir($file->filename);
  }
  drush_copy_dir($project_dir, $checkout_dir, FILE_EXISTS_MERGE);

  // Commit local version.
  if (!drush_shell_exec('git add --all .')) {
    return drush_set_error('CANNOT_ADD');
  }
  if (!drush_shell_exec('git commit -m ' . drush_escapeshellarg("Committing local."))) {
    return drush_set_error('CANNOT_COMMIT');
  }

  // d.o automatically adds the LICENSE.txt, so we remove it now, in order to
  // not polute the diff.
  if (file_exists('LICENSE.txt')) {
    unlink('LICENSE.txt');
  }
  if (!_bandaid_fix_info_files($checkout_dir)) {
    return drush_set_error('INFO_FILE_ERROR');
  }

  // Create a diff to show the user.
  if (!drush_shell_exec('git diff ' . $checkout_ref)) {
    return drush_set_error('CANNOT_DIFF');
  }
  $patch = implode("\n", drush_shell_exec_output());
  if (!empty($patch)) {
    drush_log(dt('Local changes detected.'), 'warning');
    drush_print($patch);
    if (!drush_confirm('Continue anyway?')) {
      return drush_user_abort();
    }
  }
  // Reset so we get the state back to the same as the local version.
  if (!drush_shell_exec('git reset --hard')) {
    return drush_set_error('CANNOT_DIFF');
  }

  // Apply patches to local branch.
  if (!empty($patches)) {
    if (!_bandaid_patch($patches)) {
      return;
    }
  }

  // Apply a local patch.
  if (file_exists($local_patch_file)) {
    if (!drush_shell_exec('GIT_DIR=. git apply -p1 %s --verbose', $local_patch_file)) {
      return drush_set_error('CANNOT_PATCH');
    }
    drush_log(dt('Patched with @filename.', array('@filename' => basename($local_patch_file))), 'ok');
    unlink($local_patch_file);
  }

  if (!drush_move_dir($project_dir, $project_dir . '.old')) {
    return FALSE;
  }

  if (!drush_delete_dir('.git')) {
    return drush_set_error('CANNOT_DELETE_GIT');
  }
  chdir(dirname($project_dir));
  if (!drush_move_dir($checkout_dir, $project_dir)) {
    return FALSE;
  }
  drush_register_file_for_deletion($project_dir . '.old');
}

function drush_bandaid_patch($patch, $project = NULL) {
  if (!$project) {
    $project_dir = realpath(drush_cwd());
    $project = basename($project_dir);
  }
  else {
    $project_dir = realpath(drush_cwd() . '/' . $project);
  }
  $yaml_file = $project_dir . '.yml';

  chdir($project_dir);
  $project = basename($project_dir);

  if (file_exists($yaml_file)) {
    $yaml = Yaml::parse($yaml_file);
  }
  else {
    $yaml = array(
      'patches' => array(),
    );
  }

  // @todo this was wholesomely copied and adjusted from _bandaid_patch, need to
  // refactor things together again.
  if ($filename = _bandaid_download_file($patch)) {
    $patched = FALSE;
    $output = '';
    // Test each patch style; -p1 is the default with git. See
    // http://drupal.org/node/1054616
    $patch_levels = array('-p1', '-p0');
    foreach ($patch_levels as $patch_level) {
      $checked = drush_shell_exec('GIT_DIR=. git apply --check %s %s --verbose', $patch_level, $filename);
      if ($checked) {
        // Apply the first successful style.
        $patched = drush_shell_exec('GIT_DIR=. git apply %s %s --verbose', $patch_level, $filename);
        break;
      }
    }

    // In some rare cases, git will fail to apply a patch, fallback to using
    // the 'patch' command.
    if (!$patched) {
      foreach ($patch_levels as $patch_level) {
        // --no-backup-if-mismatch here is a hack that fixes some
        // differences between how patch works on windows and unix.
        if ($patched = drush_shell_exec("patch %s --no-backup-if-mismatch < %s", $patch_level, $filename)) {
          break;
        }
      }
    }

    if ($output = drush_shell_exec_output()) {
      // Log any command output, visible only in --verbose or --debug mode.
      drush_log(implode("\n", $output));
    }
  }
  else {
    return drush_set_error(CANNOT_DOWNLOAD_PATCH);
  }

  if ($patched) {
    drush_log(dt('Successfully patched.'));
    $new_patch = array(
      'patch' => $patch,
    );

    $home = drush_get_option('home', NULL);
    if (is_null($home)) {
      $home = drush_prompt(dt("Issue (or other web page) of patch?"), NULL, FALSE);
    }
    if (!empty($home)) {
      $new_patch['home'] = $home;
    }

    $reason = drush_get_option('reason', NULL);
    if (is_null($reason)) {
      // Find an editor.
      foreach (array('GIT_EDITOR', 'VISUAL', 'EDITOR') as $var) {
        $editor = getenv($var);
        drush_print($var . ':' . $editor);
        if ($editor) {
          break;
        }
      }

      if ($editor) {
        $reason_file_name = drush_tempnam('bandaid_');
        $reason_content = "

# Patch: $patch" . (!empty($home) ? "\n# Home: " . $home : '') . "
# Enter your explanation for adding this patch above.
# Lines staring with # will be removed";
        file_put_contents($reason_file_name, $reason_content);
        drush_shell_exec_interactive($editor . " %s", $reason_file_name);
        $reason = explode("\n", file_get_contents($reason_file_name));
        $reason = array_filter($reason, function ($line) {
                    return $line[0] != '#';
                  }
        );
        $reason = trim(implode("\n", $reason));
      }
      else {
        // Fall back to using drush_prompt.
        $reason = drush_prompt(dt("Reason for patching?"), NULL, FALSE);
        if (!empty($reason)) {
          $new_patch['reason'] = $reason;
        }
      }
    }

    if (!empty($reason)) {
      $new_patch['reason'] = $reason;
    }

    $yaml['patches'][] = $new_patch;
    // Only switch to inline format at level 4.
    file_put_contents($yaml_file, Yaml::dump($yaml, 4, 2));
  }
  else {
    drush_set_error(PATCHING_FAILED);
  }
}

function _bandaid_parse_info($file) {
  if ($info = drush_drupal_parse_info_file($file)) {
    if (empty($info['project'])) {
      return drush_set_error('NO_PROJECT');
    }
    if (empty($info['version'])) {
      return drush_set_error('NO_VERSION');
    }
    // Possibilities: 7.x-1.4 7.x-1.4+3-dev 7.x-2.0-alpha8+33-dev 7.x-1.x-dev
    if (!preg_match('{^(?P<core>\\d\\.x)-(?P<major>\\d+)\\.(x-dev|(?P<minor>\\d+(?:-[a-z]+\\d+)?)(?:\\+(?P<commits>\\d+)-dev)?)}', $info['version'], $matches)) {
      return drush_set_error('BAD_VERSION', dt('Could not parse version "@version"', array('@version' => $info['version'])));
    }
    return array(
      'project' => $info['project'],
      'core' => $matches['core'],
      'version' => $matches['major'] . '.' . $matches['minor'],
      'major' => $matches['major'],
      'commits' => empty($matches['commits']) ? '' : $matches['commits'],
    );
  }
  return FALSE;
}

/**
 * Creates and returns the namd of a teporary directory.
 */
function _bandaid_tmpdir() {
  $tmp_dir = rtrim(drush_find_tmp(), '/');
  $tmp_dir .= '/bandaid_tmp_' . time() . '_' . uniqid();
  if (!drush_mkdir($tmp_dir)) {
    return FALSE;
  }
  drush_register_file_for_deletion($tmp_dir);
  return $tmp_dir;
}

function _bandaid_patch($patches) {
  $local_patches = array();
  foreach ($patches as $patch) {
    // @todo something like $this->preprocessLocalFileUrl($info); in make

    // Download the patch.
    if ($filename = _bandaid_download_file($patch['patch'])) {
      $patched = FALSE;
      $output = '';
      // Test each patch style; -p1 is the default with git. See
      // http://drupal.org/node/1054616
      $patch_levels = array('-p1', '-p0');
      foreach ($patch_levels as $patch_level) {
        $checked = drush_shell_exec('GIT_DIR=. git apply --check %s %s --verbose', $patch_level, $filename);
        if ($checked) {
          // Apply the first successful style.
          $patched = drush_shell_exec('GIT_DIR=. git apply %s %s --verbose', $patch_level, $filename);
          break;
        }
      }

      // In some rare cases, git will fail to apply a patch, fallback to using
      // the 'patch' command.
      if (!$patched) {
        foreach ($patch_levels as $patch_level) {
          // --no-backup-if-mismatch here is a hack that fixes some
          // differences between how patch works on windows and unix.
          if ($patched = drush_shell_exec("patch %s --no-backup-if-mismatch < %s", $patch_level, $filename)) {
            break;
          }
        }
      }

      if ($output = drush_shell_exec_output()) {
        // Log any command output, visible only in --verbose or --debug mode.
        drush_log(implode("\n", $output));
      }

      // Set up string placeholders to pass to dt().
      $dt_args = array(
        '@filename' => basename($filename),
      );

      if ($patched) {
        $patch_url = $patch['patch'];

        // @todo If this is a local patch, copy that into place as well.
        // if ($info['local']) {
        //   $local_patches[] = $info['url'];
        //   // Use a local path for the PATCHES.txt file.
        //   $pathinfo = pathinfo($patch_url);
        //   $patch_url = $pathinfo['basename'];
        // }
        drush_log(dt('Patched with @filename.', $dt_args), 'ok');
        $message = "Patched with " . $patch['patch'];
        if (!empty($patch['reason'])) {
          drush_log(dt('(@reason)', array('@reason' => $patch['reason'])), 'ok');
          $message = "\n\n" . $patch['reason'];
        }
        if (!drush_shell_exec('git add --all .')) {
          return drush_set_error('CANNOT_ADD');
        }
        if (!drush_shell_exec('git commit -m ' . drush_escapeshellarg($message))) {
          return drush_set_error('CANNOT_COMMIT');
        }
      }
      else {
        return drush_set_error('PATCH_ERROR', dt("Unable to patch with @filename.", $dt_args));
      }
      drush_op('unlink', $filename);
    }
    else {
      return drush_set_error('DOWNLOAD_ERROR', 'Unable to download ' . $patch['patch'] . '.');
    }
  }
  return TRUE;
}

/**
 * Wrapper to drush_download_file().
 *
 * Originally stolen from Drush make.
 *
 * @param string $download
 *   The url of the file to download.
 * @param int $cache_duration
 *   The time in seconds to cache the resultant download.
 *
 * @return string
 *   The location of the downloaded file, or FALSE on failure.
 */
function _bandaid_download_file($download, $cache_duration = DRUSH_CACHE_LIFETIME_DEFAULT) {
  if (drush_get_option('no-cache', FALSE)) {
    $cache_duration = 0;
  }

  $tmp_path = _bandaid_tmpdir();
  if (!$tmp_path) {
    return FALSE;
  }
  // Ensure that we aren't including the querystring when generating a filename
  // to save our download to.
  $file = basename(current(explode('?', $download, 2)));
  return drush_download_file($download, $tmp_path . '/' . $file, $cache_duration);
}

function _bandaid_fix_info_files($dir) {
  $info_files = drush_scan_directory($dir, '/.*\.info$/');
  foreach ($info_files as $info_file) {
    $content = file_get_contents($info_file->filename);
    // Yeah, ugly, but d.o adds one newline, while drush adds two. And don't get
    // me started on the one versus two spaces or that d.o seems to be random in
    // its capitalization. At least we know we follow the d.o format ourselves.
    $content = preg_replace('/\\n;\s+Information added by ([Dd]rupal.org|bandaid).*$/s', '', $content);
    $content = preg_replace('/\\n\\n;\s+Information added by drush.*$/s', '', $content);
    if (!file_put_contents($info_file->filename, $content)) {
      return FALSE;
    }
  }
  return TRUE;
}

/**
 * Inject metadata into all .info files for a given project.
 *
 * Just like d.o and drush does.
 * Originally from: http://drupalcode.org/project/drupalorg.git/blob/refs/heads/6.x-3.x:/drupalorg_project/plugins/release_packager/DrupalorgProjectPackageRelease.class.php#l192
 *
 * @param string $project_dir
 *   The full path to the root directory of the project to operate on.
 * @param string $project_name
 *   The project machine name (AKA shortname).
 * @param string $version
 *   The version string to inject into the .info file(s).
 *
 * @return boolean
 *   TRUE on success, FALSE on any failures appending data to .info files.
 */
function _bandaid__inject_info_file_metadata($project_dir, $project_name, $version) {
  $info_files = drush_scan_directory($project_dir, '/.*\.info$/');
  if (!empty($info_files)) {
    $info = "\n; Information added by bandaid on " . date('Y-m-d') . "\n";
    $info .= "version = \"$version\"\n";
    $matches = array();
    if (preg_match('/^((\d+)\.x)-.*/', $version, $matches) && $matches[2] >= 6) {
      $info .= "core = \"$matches[1]\"\n";
    }
    // @todo should add this option.
    if (!drush_get_option('no-gitprojectinfo', FALSE)) {
      $info .= "project = \"$project_name\"\n";
    }
    $info .= 'datestamp = "' . time() . "\"\n";
    $info .= "\n";
    foreach ($info_files as $info_file) {
      if (!drush_file_append_data($info_file->filename, $info)) {
        return FALSE;
      }
    }
  }
  return TRUE;
}
