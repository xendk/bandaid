<?php

/**
 * @file
 * Bandaid for Drush.
 *
 * Warning: This is still much a proof of concept, with duplicate code, harcoded
 * values, missing documentation and lacking error messages. Those of a sensible
 * dispossition should avert their eyes.
 */

require 'bandaid.inc';

use Bandaid\Git;
use Bandaid\BandaidError;
use Symfony\Component\Yaml\Yaml;
define('BANDAID_MINIMUM_PHP', '5.3.0');
// Cache for a week. Patches shouldn't change.
define('BANDAID_CACHE_LIFETIME_DEFAULT', 604800);

/**
 * Implements hook_drush_command().
 */
function bandaid_drush_command() {
  $local_vendor_path = __DIR__ . '/vendor/autoload.php';
  $global_vendor_path = __DIR__ . '/../../../vendor/autoload.php';

  // Check for a local composer install or a global composer install. Vendor
  // dirs are in different spots).
  if ((!@include $local_vendor_path) && (!@include $global_vendor_path)) {
    drush_log("Unable to load autoload.php. Bandaid requires Composer in order to install its depedencies and autoload classes.", 'warning');
    return array();
  }

  $items['bandaid-patch'] = array(
    'description' => 'Add a patch.',
    'arguments' => array(
      'patch' => 'URL of patch to apply.',
      'project' => 'Path to project to patch. If left out, current dir is pactched.',
    ),
    'options' => array(
      'home' => 'URL of issue.',
      'reason' => 'Reason for patch.',
      'editor' => 'Editor command to use for reason if not given by --reason',
      'no-editor' => "Don't use an editor, prompt on command line instead.",
      'no-cache' => "Don't use patch download cache.",
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('bp'),
  );

  $items['bandaid-diff'] = array(
    'description' => 'Show local diff.',
    'arguments' => array(
      'project' => 'Path to project to show diff for. Default to current dir.',
      'file' => 'File to save diff to, optional.',
    ),
    'options' => array(
      'no-cache' => "Don't use patch download and git repo cache.",
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('bd'),
  );

  $items['bandaid-tearoff'] = array(
    'description' => 'Tear off patches.',
    'arguments' => array(
      'project' => 'Path to project to remove patches from. Default to current dir.',
    ),
    'options' => array(
      'no-cache' => "Don't use patch download and git repo cache.",
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('bt'),
  );

  $items['bandaid-apply'] = array(
    'description' => 'Reapply patches.',
    'arguments' => array(
      'project' => 'Path to project to apply patches to. Default to current dir.',
    ),
    'options' => array(
      'no-cache' => "Don't use patch download and git repo cache.",
      'ignore-failing' => "Skip patches that fails to apply.",
      'update-yaml' => "Remove failed patches from YAML file.",
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('ba'),
  );

  return $items;
}

/**
 * Implements hook_drush_help().
 */
function bandaid_drush_help($section) {
  $help = NULL;
  switch ($section) {
    case 'meta:bandaid:title':
      $help = dt('Bandaid');
      break;

    case 'meta:bandaid:summary':
      $help = dt('Bandaid patch management.');
      break;

    case 'drush:bandaid-patch':
      $help = array(
        dt('Apply a patch.'),
        '',
        dt('Will download the patch and apply it to the project. Makes a note of it in the <project>.yml file.'),
      );
      break;

    case 'drush:bandaid-diff':
      $help = array(
        dt('Outputs the diff of the local modifications.'),
        '',
        dt('Attempts to remove the patches from the <project>.yml files from the project outputs a diff with any remaining changes.'),
      );
      break;

    case 'drush:bandaid-tearoff':
      $help = array(
        dt('Removes all patches from project.'),
        '',
        dt('Attempts to remove the patches from the <project>.yml files from the project and creates a <project>.local.patch file with any remaining changes.'),
      );
      break;

    case 'drush:bandaid-apply':
      $help = array(
        dt('Reapply patches.'),
        '',
        dt('Reapplies the patches from the <project.yml> file and any found
<project>.local.patch file to the project.'),
      );
      break;
  }
  return $help;
}

/**
 * Command callback.
 *
 * Download and apply a patch to a project.
 */
function drush_bandaid_patch($patch = NULL, $project = NULL) {
  try {
    if (!$patch) {
      throw new BandaidError('NO_PATCH', dt('What do you suppose I should patch WITH?'));
    }
    _bandaid_check_php_version();

    $project = _bandaid_validate_project($project);
    if (empty($project['yaml'])) {
      $project['yaml'] = array(
        'patches' => array(),
      );
    }
    chdir($project['dir']);

    // @todo this was wholesomely copied and adjusted from _bandaid_patch, need
    // to refactor things together again.
    $filename = _bandaid_download_file($patch);
    $patched = FALSE;
    $output = '';
    // Test each patch style; -p1 is the default with git. See
    // http://drupal.org/node/1054616
    $patch_levels = array(1, 0);
    foreach ($patch_levels as $patch_level) {
      $checked = Git::applyCheck($filename, $patch_level);
      if ($checked) {
        // Apply the first successful style.
        $patched = Git::apply($patch_level, $filename);
        break;
      }
    }

    // In some rare cases, git will fail to apply a patch, fallback to using
    // the 'patch' command.
    if (!$patched) {
      foreach ($patch_levels as $patch_level) {
        // --no-backup-if-mismatch here is a hack that fixes some
        // differences between how patch works on windows and unix.
        if ($patched = drush_shell_exec("patch -p%s --no-backup-if-mismatch < %s", $patch_level, $filename)) {
          break;
        }
      }
    }

    if ($output = drush_shell_exec_output()) {
      // Log any command output, visible only in --verbose or --debug mode.
      drush_log(implode("\n", $output));
    }

    if (!$patched) {
      throw new BandaidError('PATCHING_FAILED', dt('Could not apply patch.'));
    }
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
      if (!drush_get_option('no-editor', FALSE)) {
        $editor = drush_get_editor();
        $reason_file_name = drush_tempnam('bandaid_');
        $reason_content = "

# Patch: $patch" . (!empty($home) ? "\n# Home: " . $home : '') . "
# Enter your explanation for adding this patch above.
# Lines staring with # will be removed";
        file_put_contents($reason_file_name, $reason_content);
        drush_shell_exec_interactive($editor, $reason_file_name);
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

    $project['yaml']['patches'][] = $new_patch;
    // Only switch to inline format at level 4.
    file_put_contents($project['yaml_file'], Yaml::dump($project['yaml'], 4, 2));
  }
  catch (BandaidError $e) {
    return $e->drushSetError();
  }
}

/**
 * Command callback.
 *
 * Remove patches from project.
 */
function drush_bandaid_tearoff($project = NULL) {
  try {
    _bandaid_check_php_version();

    $project = _bandaid_validate_project($project);

    chdir(dirname($project['dir']));

    if ($project['local_patch']) {
      drush_log(dt('Local patch file exists, it will be overwritten.'), 'warning');
      if (!drush_confirm('Continue anyway?')) {
        return drush_user_abort();
      }
    }

    list($checkout_dir, $checkout_ref, $patch) = _bandaid_clone_and_tearoff($project);

    // Create local patch file.
    if (!empty($patch)) {
      if (!file_put_contents($project['local_patch_file'], $patch)) {
        throw new BandaidError('CANNOT_CREATE_PATCH', dt('Error creating file @patch_file', array('@patch_file' => $project['local_patch_file'])));
      }
    }

    // Return to a pristine checkout.
    Git::checkout($checkout_ref);

    // Recreate LICENSE.txt if needed.
    if (!empty($license)) {
      file_put_contents('LICENSE.txt', $license);
    }
    if (!empty($license_perms)) {
      chmod('LICENSE.txt', $license_perms);
    }

    // Fiddle with info files like d.o and drush.
    $version = $project['version_info']['core'] . '-' .
      $project['version_info']['version'] .
      (!empty($project['version_info']['commits']) ?
        '+' . $project['version_info']['commits'] . '-dev' :
        '');
    _bandaid_inject_info_file_metadata($checkout_dir, $project['name'], $version);

    chdir(dirname($project['dir']));
    _bandaid_overwrite_project($checkout_dir, $project['dir']);
  }
  catch (BandaidError $e) {
    return $e->drushSetError();
  }
}

/**
 * Command callback.
 *
 * Show diff of local modifications to a project.
 */
function drush_bandaid_diff($project = NULL, $file = NULL) {
  try {
    _bandaid_check_php_version();

    $project = _bandaid_validate_project($project);

    $cwd = getcwd();
    chdir(dirname($project['dir']));

    list($checkout_dir, $checkout_ref, $patch) = _bandaid_clone_and_tearoff($project);

    // Create local patch file.
    if (!empty($patch)) {
      if ($file) {
        chdir($cwd);
        file_put_contents($file, $patch);
        drush_log(dt('Saved to @file.', array('@file' => $file)), 'ok');
      }
      else {
        drush_print($patch);
      }
    }

    chdir($checkout_dir);
    // Return to a pristine checkout.
    Git::checkout($checkout_ref);
    $filename = drush_save_data_to_temp_file($patch);
    if (!Git::applyCheck($filename, 1)) {
      drush_log(dt('Patch does not apply cleanly to base revision.'), 'warning');
    }

  }
  catch (BandaidError $e) {
    return $e->drushSetError();
  }
}

/**
 * Command callback.
 *
 * Reapplies patches to project.
 */
function drush_bandaid_apply($project = NULL) {
  try {
    _bandaid_check_php_version();

    $project = _bandaid_validate_project($project);

    chdir(dirname($project['dir']));

    // Clone repo.
    drush_log(dt('Cloning @name.', array('@name' => $project['name'])), 'status');
    $checkout_dir = _bandaid_tmpdir();
    !_bandaid_clone($project['name'], 'http://git.drupal.org/project/' . $project['version_info']['project'], $checkout_dir);
    drush_register_file_for_deletion(realpath($checkout_dir));

    chdir($checkout_dir);

    list($checkout_ref, $description) = _bandaid_sha_from_info($project['version_info']);

    // Checkout base version.
    drush_log(dt('Checking out @ref.', array('@ref' => $checkout_ref)), 'status');
    if (!empty($description)) {
      drush_log(dt('Commit message: @desc.', array('@desc' => $description)), 'status');
    }
    Git::checkout($checkout_ref);

    // Some have erranously committed LICENSE.txt. Note if that's the case for
    // later.
    $license_exists = file_exists('LICENSE.txt');
    if ($license_exists) {
      $license_perms = fileperms('LICENSE.txt');
    }

    // Create local branch.
    Git::branch('bandaid_local');

    // Copy the local project to the local branch.
    Git::checkout('bandaid_local');

    // Clear out the files in the repo and copy in the files from the project.
    _bandaid_replace_repo($checkout_dir, $project['dir']);

    // Commit local version.
    Git::add('.', TRUE);
    Git::commit("Committing local.");

    // d.o automatically adds the LICENSE.txt, so we remove it now, in order to
    // not polute the diff, unless it *was* committed.
    if (file_exists('LICENSE.txt')) {
      if (!$license_exists) {
        unlink('LICENSE.txt');
      }
      else {
        chmod('LICENSE.txt', $license_perms);
      }
    }
    _bandaid_fix_info_files($checkout_dir);

    // Create a diff to show the user.
    $patch = Git::diff($checkout_ref);
    if (!empty($patch)) {
      drush_log(dt('Local changes detected, these will be lost if continuing.'), 'warning');
      drush_print($patch);
      if (!drush_confirm('Continue anyway?')) {
        return drush_user_abort();
      }
    }
    // Reset so we get the state back to the same as the local version.
    Git::reset(TRUE);

    // Apply patches to local branch.
    if (!empty($project['yaml']['patches'])) {
      $new_patches = _bandaid_patch($project['yaml']['patches']);
    }

    // Apply a local patch.
    if ($project['local_patch']) {
      if (Git::apply($project['local_patch_file'])) {
        drush_log(dt('Patched with @filename.', array('@filename' => basename($project['local_patch_file']))), 'ok');
        unlink($project['local_patch_file']);
      }
      else {
        drush_log(dt('Error applying local patch. Please apply it manually.'), 'error');
      }
    }

    chdir(dirname($project['dir']));
    _bandaid_overwrite_project($checkout_dir, $project['dir']);

    // If some patches were ignored, write new yaml file.
    if (($new_patches != $project['yaml']['patches']) && drush_get_option('update-yaml', FALSE)) {
      $project['yaml']['patches'] = $new_patches;
      if (!file_put_contents($project['yaml_file'], Yaml::dump($project['yaml'], 4, 2))) {
        throw new BandaidError('CANNOT_WRITE_YAML', dt('Error writing new YAML file @file', array('@file' => $project['yaml_file'])));
      }
      drush_log('Updated yaml file.', 'ok');
    }
  }
  catch (BandaidError $e) {
    return $e->drushSetError();
  }
}

/**
 * Check that PHP is high enough a version.
 */
function _bandaid_check_php_version() {
  if (version_compare(phpversion(), BANDAID_MINIMUM_PHP) < 0) {
    throw new BandaidError('BANDAID_REQUIREMENTS_ERROR', dt('Your command line PHP installation is too old. Bandaid requires at least PHP !version.', array('!version' => DRUSH_MINIMUM_PHP)));
  }
  return TRUE;
}

/**
 * Validate project.
 *
 * Ensure we can find it, parse the info file, check if there's any local patch
 * file, and load any found yaml file.
 *
 * If project not given, try the current dir.
 */
function _bandaid_validate_project($project_name = NULL) {
  if (!$project_name) {
    $project_name = getcwd();
  }
  else {
    $project_name = realpath($project_name);
  }
  $project = array(
    'name' => basename($project_name),
    'dir' => $project_name,
    'version_info' => NULL,
    'local_patch' => NULL,
    'yaml' => NULL,
  );
  $info_file = $project['dir'] . '/' . $project['name'] . '.info';

  if (!file_exists($info_file)) {
    throw new BandaidError('NO_INFO_FILE', dt('No info file in @path?', array('@path' => $project['dir'])));
  }

  try {
    $project['version_info'] = _bandaid_parse_info($info_file);
  }
  catch (BandaidError $e) {
    throw new BandaidError('COULD_NOT_PARSE_INFO', dt('Invalid info file @file', array('@file' => $info_file)), $e);
  }

  $project['local_patch_file'] = $project['dir'] . '.local.patch';
  if (file_exists($project['local_patch_file'])) {
    $project['local_patch'] = TRUE;
  }

  $project['yaml_file'] = $project['dir'] . '.yml';
  if (file_exists($project['yaml_file'])) {
    $project['yaml'] = Yaml::parse($project['yaml_file']);
  }

  return $project;
}

/**
 * Parse a modules info file.
 *
 * Figures out which version, and possibly patch level a module is.
 */
function _bandaid_parse_info($file) {
  $info = drush_drupal_parse_info_file($file);
  if (!$info) {
    throw new BandaidError('COULD_NOT_PARSE_INFO_FILE', dt('Error parsing the info file.'));
  }

  if (empty($info['project'])) {
    throw new BandaidError('NO_PROJECT', dt('No "project" in info file.'));
  }

  if (empty($info['version'])) {
    throw new BandaidError('NO_VERSION', dt('No "version" in info file.'));
  }

  return _bandaid_parse_version($info['version']) + array('project' => $info['project']);
}

/**
 * Parse a version string.
 */
function _bandaid_parse_version($version) {
  // Possibilities: 7.x-1.4 7.x-1.4+3-dev 7.x-2.0-alpha8+33-dev 7.x-1.x-dev
  if (!preg_match('{^(?P<core>\\d\\.x)-(?P<major>\\d+)\\.(x-dev|(?P<minor>\\d+(?:-[a-z]+\\d+)?)(?:\\+(?P<commits>\\d+)-dev)?)}', $version, $matches)) {
    throw new BandaidError('BAD_VERSION', dt('Could not parse version "@version"', array('@version' => $info['version'])));
  }
  if (empty($matches['minor'])) {
    throw new BandaidError('RAW_DEV_NOT_SUPPORTED', dt('Dev releases without a patch level is not supported.'));
  }
  return array(
    'core' => $matches['core'],
    'version' => $matches['major'] . '.' . $matches['minor'],
    'major' => $matches['major'],
    'commits' => empty($matches['commits']) ? '' : $matches['commits'],
  );
}

/**
 * Clones project and does a tearoff.
 *
 * Returns path to clone, base ref and local patch contents.
 */
function _bandaid_clone_and_tearoff($project) {
  // Clone repo.
  drush_log(dt('Cloning @name.', array('@name' => $project['name'])), 'status');
  $checkout_dir = _bandaid_tmpdir();
  _bandaid_clone($project['name'], 'http://git.drupal.org/project/' . $project['version_info']['project'], $checkout_dir);
  drush_register_file_for_deletion($checkout_dir);

  chdir($checkout_dir);

  list($checkout_ref, $description) = _bandaid_sha_from_info($project['version_info']);

  // Checkout base version.
  drush_log(dt('Checking out @ref.', array('@ref' => $checkout_ref)), 'status');
  if (!empty($description)) {
    drush_log(dt('Commit message: @desc.', array('@desc' => $description)), 'status');
  }
  Git::checkout($checkout_ref);

  // Some have erroneously committed LICENSE.txt. Note if that's the case for
  // later.
  $license_exists = file_exists('LICENSE.txt');
  if ($license_exists) {
    $license_perms = fileperms('LICENSE.txt');
  }

  // Create patch branch.
  Git::branch('bandaid_patch');

  // Create local branch.
  Git::branch('bandaid_local');

  // Apply patches to patch branch.
  if (!empty($project['yaml']['patches'])) {
    Git::checkout('bandaid_patch');
    _bandaid_patch($project['yaml']['patches']);
  }

  // Copy the local project to the local branch.
  Git::checkout('bandaid_local');

  // Clear out the files in the repo and copy in the files from the project.
  _bandaid_replace_repo($checkout_dir, $project['dir']);

  // d.o automatically adds the LICENSE.txt, so we remove it now, in order to
  // not polute the diff here, but add it again when creating the new project
  // from git, in order not to polute the diff in the site. Unless it was
  // committed to git, in case we just carry over the file permissions, as d.o
  // might still mess with those.
  if (file_exists('LICENSE.txt')) {
    if (!$license_exists) {
      $license_perms = fileperms('LICENSE.txt');
      $license = file_get_contents('LICENSE.txt');
      unlink('LICENSE.txt');
    }
    else {
      chmod('LICENSE.txt', $license_perms);
    }
  }
  _bandaid_fix_info_files($checkout_dir);

  Git::add('.', TRUE);
  Git::commit("Committing local.");

  // Create local patch.
  $patch = Git::diff('bandaid_patch', 'bandaid_local');

  return array($checkout_dir, $checkout_ref, $patch);
}

/**
 * Creates and returns the name of a temporary directory.
 */
function _bandaid_tmpdir() {
  $system_tmp = rtrim(drush_find_tmp(), '/');
  $tmp_name = 'bandaid_tmp_' . time() . '_' . uniqid();
  $tmp_dir = $system_tmp . '/' . $tmp_name;
  if (!drush_mkdir($tmp_dir)) {
    throw new BandaidError('CANNOT_CREATE_TEMP_DIR', dt('Cannot create temporary directory "@dir". Please check permissions for @tmp.', array('@dir' => $tmp_name, '@tmp' => $system_tmp)));
  }
  drush_register_file_for_deletion($tmp_dir);
  return $tmp_dir;
}

/**
 * Find the SHA for the version specified by an info file.
 *
 * Handles dev releases.
 *
 * @retern array
 *   Two element array of sha and mesage. Message is empty for non-dev releases.
 */
function _bandaid_sha_from_info($version_info) {
  $branch       = 'origin/' . $version_info['core'] . '-' . $version_info['major'] . '.x';
  $tag          = $version_info['core'] . '-' . $version_info['version'];
  $checkout_ref = $version_info['core'] . '-' . $version_info['version'];
  if ($version_info['commits']) {
    // Get history from base version to head.  Flip the order so we have the
    // oldest commit first.
    $commits = array_reverse(Git::onelineLog($tag, $branch));
    // Pick out the nth commit, and split the line.
    return explode(' ', $commits[$version_info['commits'] - 1], 2);
  }
  return array($checkout_ref, NULL);
}

/**
 * Clone a repo, with caching.
 *
 * Uses the same cache as Drush make, code mostly lifted from makes
 * implementation.
 */
function _bandaid_clone($name, $repo, $path) {
  $cache = !drush_get_option('no-cache', FALSE);
  if ($cache && ($git_cache = drush_directory_cache('git'))) {
    $project_cache = $git_cache . '/' . $name . '-' . md5($repo);
    // Set up a new cache, if it doesn't exist.
    if (!file_exists($project_cache)) {
      $options = array(
        'mirror' => TRUE,
      );
      if (drush_get_context('DRUSH_VERBOSE')) {
        $options += array(
          'verbose' => TRUE,
          'progress' => TRUE,
        );
      }
      Git::cloneRepository($repo, $project_cache, $options);
    }
    else {
      // Update the --mirror clone.
      $dir = getcwd();
      chdir($project_cache);
      Git::remoteUpdate();
      chdir($dir);
    }
    $git_cache = $project_cache;
  }

  $options = array();
  if (drush_get_context('DRUSH_VERBOSE')) {
    $options += array(
      'verbose' => TRUE,
      'progress' => TRUE,
    );
  }
  if ($cache) {
    $options['reference'] = $git_cache;
  }

  try {
    Git::cloneRepository($repo, $path, $options);
  }
  catch (BandaidError $e) {
    // Throw a new Exception so we can get the project name in.
    throw new BandaidError('CANNOT_CLONE', dt('Unable to clone @project from @url.', array('@project' => $name, '@url' => $url)));
  }
  return TRUE;
}

/**
 * Replace the files of a repository with the content of a directory.
 */
function _bandaid_replace_repo($repository, $source) {
  $ignore_files = array('.', '..', '.git');
  foreach (drush_scan_directory($repository, '{.*}', $ignore_files, 0, FALSE, 'filename', 0, TRUE) as $file) {
    drush_delete_dir($file->filename);
  }
  drush_copy_dir($source, $repository, FILE_EXISTS_MERGE);
}

/**
 * Apply patches to project.
 *
 * @todo Handle local files.
 */
function _bandaid_patch($patches) {
  $processed_patches = array();
  foreach ($patches as $patch) {
    // Download the patch.
    $filename = _bandaid_download_file($patch['patch']);
    $patched = FALSE;
    $output = '';

    // Set up string placeholders to pass to dt().
    $dt_args = array(
      '@filename' => basename($filename),
    );

    // Test each patch style; -p1 is the default with git. See
    // http://drupal.org/node/1054616
    $patch_levels = array(1, 0);
    foreach ($patch_levels as $patch_level) {
      $checked = Git::applyCheck($filename, $patch_level);
      if ($checked) {
        // Apply the first successful style.
        $patched = Git::apply($filename, $patch_level);
        break;
      }
    }

    // In some rare cases, git will fail to apply a patch, fallback to using
    // the 'patch' command.
    if (!$patched) {
      foreach ($patch_levels as $patch_level) {
        // --no-backup-if-mismatch here is a hack that fixes some
        // differences between how patch works on windows and unix.
        if ($patched = drush_shell_exec("patch -p%s --no-backup-if-mismatch < %s", $patch_level, $filename)) {
          break;
        }
      }
    }

    if ($output = drush_shell_exec_output()) {
      // Log any command output, visible only in --verbose or --debug mode.
      drush_log(implode("\n", $output));
    }

    if ($patched) {
      $processed_patches[] = $patch;
      $patch_url = $patch['patch'];

      drush_log(dt('Patched with @filename.', $dt_args), 'ok');
      $message = "Patched with " . $patch['patch'];
      if (!empty($patch['reason'])) {
        drush_log(dt('(@reason)', array('@reason' => $patch['reason'])), 'ok');
        $message = "\n\n" . $patch['reason'];
      }
      Git::add('.', TRUE);
      Git::commit($message);
      drush_op('unlink', $filename);
    }
    else {
      if (drush_get_option('ignore-failing', FALSE)) {
        drush_log(dt("Unable to patch with @filename, skipping.", $dt_args), 'error');
        drush_op('unlink', $filename);

      }
      else {
        throw new BandaidError('PATCH_ERROR', dt("Unable to patch with @filename.", $dt_args));
      }
    }
  }
  return $processed_patches;
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
function _bandaid_download_file($download, $cache_duration = BANDAID_CACHE_LIFETIME_DEFAULT) {
  if (drush_get_option('no-cache', FALSE)) {
    $cache_duration = 0;
  }

  $tmp_path = _bandaid_tmpdir();
  // Ensure that we aren't including the querystring when generating a filename
  // to save our download to.
  $file = basename(current(explode('?', $download, 2)));
  $tmp_file = drush_download_file($download, $tmp_path . '/' . $file, $cache_duration);
  if (!$tmp_file) {
    throw new BandaidError('COULD_NOT_DOWNLOAD_FILE', dt('Could not download @url', array('@url' => $download)));
  }
  return $tmp_file;
}

/**
 * Removes d.o/drush/bandaid metadata from info files.
 */
function _bandaid_fix_info_files($dir) {
  $info_files = drush_scan_directory($dir, '/.*\.info$/');
  foreach ($info_files as $info_file) {
    $content = file_get_contents($info_file->filename);
    $lines = explode("\n", $content);
    for ($i = count($lines) - 1; $i > -1; $i--) {
      // The \s+ gobbles up both drushs 2 spaces and d.o and our one. Seems d.o
      // fixed their capitalization at some point.
      if (preg_match('/;\s+Information added by ([Dd]rupal.org|bandaid|drush).*$/', $lines[$i], $matches)) {
        // Drush adds two newlines in front, instead of the one d.o and we do.
        $strip_from = $matches[1] == 'drush' ? $i - 1 : $i;
        break;
      }
    }
    if ($i) {
      $lines = array_slice($lines, 0, $i);
      if (!file_put_contents($info_file->filename, implode("\n", $lines))) {
        throw new BandaidError('CANNOT_WRITE_INFO', dt('Could not write info file @file', array('@file' => $info_file->filename)));
      }
    }
  }
  return TRUE;
}

/**
 * Inject metadata into all .info files for a given project.
 *
 * Just like d.o and drush does.
 *
 * @param string $project_dir
 *   The full path to the root directory of the project to operate on.
 * @param string $project_name
 *   The project machine name (AKA shortname).
 * @param string $version
 *   The version string to inject into the .info file(s).
 *
 * @return bool
 *   TRUE on success, FALSE on any failures appending data to .info files.
 */
function _bandaid_inject_info_file_metadata($project_dir, $project_name, $version) {
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

/**
 * Overwrite project directory with our working directory.
 */
function _bandaid_overwrite_project($work_dir, $project_dir) {
  if (!drush_delete_dir($work_dir . '/.git')) {
    throw new BandaidError('CANNOT_DELETE_GIT', dt('Error deleting .git directory in working directory.'));
  }

  if (!drush_move_dir($project_dir, $project_dir . '.old')) {
    throw new BandaidError('CANNOT_MOVE', dt('Cannot move old project directory.'));
  }

  chdir(dirname($project_dir));
  if (!drush_move_dir($work_dir, $project_dir)) {
    throw new BandaidError('CANNOT_MOVE', dt('Cannot move working directory into place.'));
  }
  drush_register_file_for_deletion($project_dir . '.old');
}
