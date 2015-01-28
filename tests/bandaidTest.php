<?php

/**
 * @file
 * PHPUnit Tests for Bandaid.
 */

// The test class changed name in Drush 7, so if we're running under Drush 5/6,
// we load a class that defines the new name as a subclass to the old.
if (class_exists('Drush_CommandTestCase', FALSE)) {
  require_once 'oldtest-shim.inc';
}

use Unish\CommandUnishTestCase;
use Unish\UnitUnishTestCase;
// Drush should supply this one.
use Symfony\Component\Yaml\Yaml;


/**
 * Deployotron testing class.
 */
class BandaidFunctionalTestCase extends CommandUnishTestCase {
  const EXIT_CODE_DIFF_DETECTED = 1;
  /**
   * Setup before running any tests.
   */
  public static function setUpBeforeClass() {
    parent::setUpBeforeClass();
    // Copy in the command file, so the sandbox can find it.
    symlink(dirname(dirname(__FILE__)) . '/bandaid.drush.inc', getenv('HOME') . '/.drush/bandaid.drush.inc');
    symlink(dirname(dirname(__FILE__)) . '/bandaid.inc', getenv('HOME') . '/.drush/bandaid.inc');

    // Need to set up git minimally for it to work (else it wont commit).
    exec('git config --global user.email "drush@example.com"');
    exec('git config --global user.name "Bandaid Test cases"');
  }

  /**
   * Setup before each test case.
   */
  public function setUp() {
    // Deployotron needs a site to run in.
    if (!file_exists($this->webroot())) {
      // We're fixing this to a specific version, in order to test core
      // patching.
      $this->setUpDrupal(1, FALSE, '7.29');
    }
    else {
      // Remove modules from previous test runs, but save the readme.
      $modules_dir = $this->webroot() . '/sites/all/modules';
      $readme = file_get_contents($modules_dir . '/README.txt');
      exec('rm -rf ' . $modules_dir . '/*');
      file_put_contents($modules_dir . '/README.txt', $readme);
    }

    // Clear drush cache to ensure that it discovers the command.
    $this->drush('cc', array('drush'));
  }

  /**
   * Test the basic patch, tearoff, upgrade, apply cycle.
   */
  public function testBasicFunctionallity1() {
    $workdir = $this->webroot() . '/sites/all/modules';
    $this->drush('dl', array('panels-3.3'), array(), NULL, $workdir);

    // Apply first patch, and check for success.
    $options = array(
      'home' => 'https://drupal.org/node/1985980',
      'reason' => 'For altering of new panes.',
      'info-file' => 'panels.info',
    );
    $patch1_string = 'drupal_alter(\'panels_new_pane\', $pane);';
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/panels'));
    $this->drush('bandaid-patch', array('https://drupal.org/files/issues/panels-new-pane-alter-1985980-5.patch', 'panels'), $options, NULL, $workdir);
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/panels'));

    // We should have a yaml file now.
    $this->assertFileExists($workdir . '/panels.yml');

    // And that the patch was added.
    $this->assertFileContains($workdir . '/panels.yml', 'https://drupal.org/files/issues/panels-new-pane-alter-1985980-5.patch');

    // And that we have a info-file entry.
    $this->assertFileContains($workdir . '/panels.yml', 'panels.info');

    $options = array(
      'home' => 'https://drupal.org/node/2098515',
      'reason' => 'To avoid notice.',
      'info-file' => 'panels.info',
    );
    $patch2_string = 'if (!isset($pane->type)) {';
    $this->assertEmpty($this->grep($patch2_string, $workdir . '/panels'));
    $this->drush('bandaid-patch', array('https://drupal.org/files/issues/undefined_property_notices_fix-2098515-2.patch', 'panels'), $options, NULL, $workdir);
    $this->assertNotEmpty($this->grep($patch2_string, $workdir . '/panels'));

    // Check that yaml file has been updated.
    $this->assertFileContains($workdir . '/panels.yml', 'https://drupal.org/files/issues/undefined_property_notices_fix-2098515-2.patch');

    // And that we have a info-file entry.
    $this->assertFileContains($workdir . '/panels.yml', 'panels.info');

    // Add a local modification to the module file.
    $content = file_get_contents($workdir . '/panels/panels.module');
    $content .= "\$var = \"Local modification.\";\n";
    file_put_contents($workdir . '/panels/panels.module', $content);

    $expected_diff = "diff --git a/panels.module b/panels.module\nindex dcc13a6..82efc4a 100644\n--- a/panels.module\n+++ b/panels.module\n@@ -1757,3 +1757,4 @@ function panels_preprocess_html(&\$vars) {\n     \$vars['classes_array'][] = check_plain(\$panel_body_css['body_classes_to_add']);\n   }\n }\n+\$var = \"Local modification.\";\n";

    // Do a diff an check that it's the expected, and that the files haven't
    // changed.
    $this->drush('bandaid-diff', array('panels'), array(), NULL, $workdir, self::EXIT_CODE_DIFF_DETECTED);
    $this->assertEquals(trim($expected_diff), trim($this->getOutput()));
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/panels'));
    $this->assertNotEmpty($this->grep($patch2_string, $workdir . '/panels'));
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/panels'));

    // And that we have a info-file entry.
    $this->assertFileContains($workdir . '/panels.yml', 'panels.info');

    $options = array(
      'info-file' => 'panels.info',
    );
    $this->drush('bandaid-tearoff', array('panels'), $options, NULL, $workdir);
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/panels'));
    $this->assertEmpty($this->grep($patch2_string, $workdir . '/panels'));
    $this->assertEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/panels'));

    // And that we have a info-file entry.
    $this->assertFileContains($workdir . '/panels.yml', 'panels.info');

    $local_patch = $workdir . '/panels.local.patch';
    // Ensure that we got a local patch file and it contains the expected.
    $this->assertFileExists($local_patch);
    $this->assertEquals($expected_diff, file_get_contents($local_patch));

    // Upgrade panels.
    $this->drush('dl', array('panels-3.4'), array('y' => TRUE), NULL, $workdir);

    // Reapply patches.
    $this->drush('bandaid-apply', array('panels'), array(), NULL, $workdir);

    // The local patch file should be gone.
    $this->assertFalse(file_exists($local_patch));

    // And the project should contain the contents of the patches.
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/panels'));
    $this->assertNotEmpty($this->grep($patch2_string, $workdir . '/panels'));
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/panels'));

  }

  /**
   * Test the basic patch, tearoff, upgrade, apply cycle.
   *
   * This time with a module that has LICENSE.txt committed and the d.o info
   * line in the info file.
   */
  public function testBasicFunctionallity2() {
    $workdir = $this->webroot() . '/sites/all/modules';
    $this->drush('dl', array('exif_custom-1.13'), array(), NULL, $workdir);

    // Apply a patch, and check for success.
    $options = array(
      'home' => 'https://drupal.org/node/2112241',
      'reason' => 'Allow for overriding when uploading multiple images.',
    );
    $patch1_string = 'if(arg(3) == \'edit-multiple\'){return;}';
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));
    $this->drush('bandaid-patch', array('https://drupal.org/files/exif_override_multiple_images-2112241-1.patch', 'exif_custom'), $options, NULL, $workdir);
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));

    // We should have a yaml file now.
    $this->assertFileExists($workdir . '/exif_custom.yml');

    // And that the patch was added.
    $this->assertFileContains($workdir . '/exif_custom.yml', 'https://drupal.org/files/exif_override_multiple_images-2112241-1.patch');

    // Add a local modification to the module file (we're prepending as they're
    // happening too much at the end of the file).
    $content = file_get_contents($workdir . '/exif_custom/exif_custom.module');
    $content = "\$var = \"Local modification.\";\n" . $content;
    file_put_contents($workdir . '/exif_custom/exif_custom.module', $content);

    $expected_diff = "diff --git a/exif_custom.module b/exif_custom.module\nindex c2bdee6..b889d52 100644\n--- a/exif_custom.module\n+++ b/exif_custom.module\n@@ -1,3 +1,4 @@\n+\$var = \"Local modification.\";\n <?php\n \n /**\n";

    // Do a diff to a file and check that it is as expected and the files
    // haven't changed..
    $diff_file = tempnam($workdir, 'patch_');
    $this->drush('bandaid-diff', array('exif_custom', $diff_file), array(), NULL, $workdir, self::EXIT_CODE_DIFF_DETECTED);
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/exif_custom'));

    $this->assertFileExists($diff_file);
    $this->assertEquals($expected_diff, file_get_contents($diff_file));

    // Tearoff the patches and check that they're gone.
    $this->drush('bandaid-tearoff', array('exif_custom'), array(), NULL, $workdir);
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));
    $this->assertEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/exif_custom'));

    $local_patch = $workdir . '/exif_custom.local.patch';
    // Ensure that we got a local patch file and it contains the expected.
    $this->assertFileExists($local_patch);
    $this->assertEquals($expected_diff, file_get_contents($local_patch));

    // Upgrade exif_custom.
    $this->drush('dl', array('exif_custom-1.14'), array('y' => TRUE), NULL, $workdir);

    // Reapply patches.
    $this->drush('bandaid-apply', array('exif_custom'), array(), NULL, $workdir);

    // The local patch file should be gone.
    $this->assertFalse(file_exists($local_patch));

    // And the project should contain the contents of the patches.
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/exif_custom'));
  }

  /**
   * We should get an error message when patching fails.
   */
  public function testPatchErrorMessage() {
    $workdir = $this->webroot() . '/sites/all/modules';
    // We use exif_custom for this test.
    $this->drush('dl', array('exif_custom-1.13'), array(), NULL, $workdir);

    // Try to patch it with a panels patch, that's sure to fail.
    $options = array(
      'home' => 'https://drupal.org/node/1985980',
      'reason' => 'For altering of new panes.',
    );
    $patch1_string = 'drupal_alter(\'panels_new_pane\', $pane);';
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));
    $this->drush('bandaid-patch 2>&1', array('https://drupal.org/files/issues/panels-new-pane-alter-1985980-5.patch', 'exif_custom'), $options, NULL, $workdir, self::EXIT_ERROR);
    $this->assertRegExp('/Could not apply patch./', $this->getOutput());
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));
  }

  /**
   * Test that a dev release is properly detected.
   *
   * Also that patch skipping works.
   */
  public function testDevPatching() {
    $workdir = $this->webroot() . '/sites/all/modules';
    // This one is a bit more involved. As dev releases are inherrently
    // unstable, we can't use a real one for testing, so we fake one instead.
    $cwd = getcwd();
    chdir($workdir);
    $this->execute('git clone http://git.drupal.org/project/snapengage');
    chdir('snapengage');
    // This is a commit 2 commits after the 7.x-1.1 release.
    $this->execute('git checkout 05fe01719cc07cbad6e9e19d123055dae3b435ed');
    // Un-gittify.
    exec('rm -rf .git');

    // Fudge the info file.
    $info = file_get_contents('snapengage.info');
    $info .= <<<EOF

  ; Information added by drupal.org packaging script on 0000-00-00
version = "7.x-1.1+2-dev"
core = "7.x"
project = "snapengage"
datestamp = "0000000000"
EOF;
    file_put_contents('snapengage.info', $info);
    chdir($cwd);

    // Apply a patch, and check for success.
    $options = array(
      'home' => 'https://drupal.org/node/1916982',
      'reason' => 'Panels support.',
    );
    $patch1_string = "Plugin to handle the 'snapengage_widget' content type";
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/snapengage'));
    $this->drush('bandaid-patch', array('https://drupal.org/files/snapengage-panels-integration-1916982-4.patch', 'snapengage'), $options, NULL, $workdir);
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/snapengage'));

    // Check that the yaml file has been updated.
    $this->assertFileContains($workdir . '/snapengage.yml', 'https://drupal.org/files/snapengage-panels-integration-1916982-4.patch');

    // Apply another patch, and check for success.
    $options = array(
      'home' => 'https://drupal.org/node/1933716',
      'reason' => 'New API.',
    );
    $patch2_string = "If enabled this allowes you to use the advanced features.";
    $this->assertEmpty($this->grep($patch2_string, $workdir . '/snapengage'));
    $this->drush('bandaid-patch', array('https://drupal.org/files/snapengage-integrate-new-api-code.patch', 'snapengage'), $options, NULL, $workdir);
    $this->assertNotEmpty($this->grep($patch2_string, $workdir . '/snapengage'));

    // Check that the yaml file has been updated.
    $this->assertFileContains($workdir . '/snapengage.yml', 'https://drupal.org/files/snapengage-integrate-new-api-code.patch');

    // Tearoff the patches and check that they're gone.
    $this->drush('bandaid-tearoff', array('snapengage'), array(), NULL, $workdir);
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/snapengage'));
    $this->assertEmpty($this->grep($patch2_string, $workdir . '/snapengage'));

    // Update module.
    $this->drush('dl', array('snapengage-1.2'), array('y' => TRUE), NULL, $workdir);

    // Check that we fail per default on failing patches.
    $this->drush('bandaid-apply 2>&1', array('snapengage'), array(), NULL, $workdir, self::EXIT_ERROR);

    // Check for the expected error message.
    $this->assertRegExp('/Unable to patch with snapengage-panels-integration-1916982-4.patch/', $this->getOutput());

    // Try again, but now with options to skip it.
    $this->drush('bandaid-apply 2>&1', array('snapengage'), array('ignore-failing' => TRUE, 'update-yaml' => TRUE), NULL, $workdir);

    // Check output.
    $this->assertRegExp('/Unable to patch with snapengage-panels-integration-1916982-4.patch/', $this->getOutput());
    $this->assertRegExp('/Updated yaml file./', $this->getOutput());

    // Check that it has been properly patched.
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/snapengage'));
    $this->assertNotEmpty($this->grep($patch2_string, $workdir . '/snapengage'));
    // Check that the yaml file contains the right patches.
    $this->assertFileNotContains($workdir . '/snapengage.yml', 'https://drupal.org/files/snapengage-panels-integration-1916982-4.patch');
    $this->assertFileContains($workdir . '/snapengage.yml', 'https://drupal.org/files/snapengage-integrate-new-api-code.patch');
  }

  /**
   * Test that drush help prints the right stuff.
   */
  public function testHelp() {
    // Check normal help listing.
    $this->drush('help');
    $output = $this->getOutput();

    $this->assertRegExp('/Bandaid: \\(bandaid\\)/', $output);
    $this->assertRegExp('/ bandaid-apply \\(ba\\) +Reapply patches\\./', $output);
    $this->assertRegExp('/ bandaid-patch \\(bp\\) +Add a patch\\./', $output);
    $this->assertRegExp('/ bandaid-tearoff \\(bt\\) +Tear off patches\\./', $output);

    // Check the little known filter listing.
    $this->drush('help', array(), array('filter' => NULL, 'y' => TRUE));
    $this->assertRegExp('/Bandaid: Bandaid patch management\\./', $this->getOutput());

    // Check the summary on each command.
    $this->drush('help', array('bandaid-patch'));
    $this->assertRegExp('/Apply a patch\\./', $this->getOutput());

    $this->drush('help', array('bandaid-tearoff'));
    $this->assertRegExp('/Removes all patches from project\\./', $this->getOutput());

    $this->drush('help', array('bandaid-apply'));
    $this->assertRegExp('/Reapply patches\\./', $this->getOutput());
  }

  /**
   * Test that degit works.
   */
  public function testDegit() {
    $workdir = $this->webroot() . '/sites/all/modules';
    $cwd = getcwd();
    chdir($workdir);
    $this->execute('git clone http://git.drupal.org/project/virtual_field.git');
    chdir('virtual_field');
    // Check out some revision.
    $this->execute('git checkout f58c1e327aeb3ec6cd3aa5bf8b2b18b06f3e0ca6');
    chdir($workdir);

    $this->drush('bandaid-degit', array('virtual_field'), array('y' => TRUE), NULL, $workdir);
    // The YAML file should have been created.
    $this->assertTrue(file_exists('virtual_field.yml'));
    $yaml = Yaml::parse('virtual_field.yml');


    $this->assertEquals('git', $yaml['project']['type']);
    $this->assertEquals('http://git.drupal.org/project/virtual_field.git', $yaml['project']['origin']);
    $this->assertEquals('f58c1e327aeb3ec6cd3aa5bf8b2b18b06f3e0ca6', $yaml['project']['revision']);

    // Ensure that the git dir has been removed.
    $this->assertFalse(file_exists('virtual_field/.git'));
    chdir($cwd);
  }

  /**
   * Test that regit works.
   */
  public function testRegit() {
    $workdir = $this->webroot() . '/sites/all/modules';
    $cwd = getcwd();
    chdir($workdir);
    $this->execute('git clone http://git.drupal.org/project/virtual_field.git');
    chdir('virtual_field');
    // Check out some revision.
    $this->execute('git checkout f58c1e327aeb3ec6cd3aa5bf8b2b18b06f3e0ca6');

    // Ungit it.
    exec('rm -rf .git');

    // Ensure that the git dir has been removed.
    $this->assertFalse(file_exists('virtual_field/.git'));

    chdir($workdir);

    // Should fail when it has no idea what the repo/revision is.
    $this->drush('bandaid-regit', array('virtual_field'), array('y' => TRUE), NULL, $workdir, self::EXIT_ERROR);

    $this->drush('bandaid-regit', array('virtual_field'), array('y' => TRUE, 'origin' => 'http://git.drupal.org/project/virtual_field.git', 'revision' => 'f58c1e327aeb3ec6cd3aa5bf8b2b18b06f3e0ca6'), NULL, $workdir);

    // Check that the .git dir exists.
    $this->assertTrue(file_exists('virtual_field/.git'));

    // The YAML file shouldn't have been created.
    $this->assertFalse(file_exists('virtual_field.yml'));

    $this->drush('bandaid-degit', array('virtual_field'), array('y' => TRUE), NULL, $workdir);
    // Check that the .git dir have been removed.
    $this->assertFalse(file_exists('virtual_field/.git'));

    $this->drush('bandaid-regit', array('virtual_field'), array('y' => TRUE, 'origin' => 'http://git.drupal.org/project/virtual_field.git', 'revision' => 'f58c1e327aeb3ec6cd3aa5bf8b2b18b06f3e0ca6'), NULL, $workdir);

    // Check that the .git dir exists.
    $this->assertTrue(file_exists('virtual_field/.git'));

    chdir($cwd);
  }

  /**
   * Test non-default branch handling.
   *
   * Regression test. Releases on the non default branch tripped up tearoff, as
   * git wouldn't recognize the branch name because it wouldn't have a local
   * tracking branch.
   */
  public function testNonDefaultBranch() {
    $workdir = $this->webroot() . '/sites/all/modules';

    // We have to fake a dev release again.
    $cwd = getcwd();
    chdir($workdir);
    $this->execute('git clone http://git.drupal.org/project/ultimate_cron');
    chdir('ultimate_cron');
    // This is a commit 2 commits after the 7.x-1.9 release.
    $this->execute('git checkout 286b82bcd00734324cc85098a494f5335f73d17e');
    // Un-gittify.
    exec('rm -rf .git');

    // Fudge the info file.
    $info = file_get_contents('ultimate_cron.info');
    $info .= <<<EOF

  ; Information added by drupal.org packaging script on 0000-00-00
version = "7.x-1.9+2-dev"
core = "7.x"
project = "ultimate_cron"
datestamp = "0000000000"
EOF;
    file_put_contents('ultimate_cron.info', $info);
    chdir($cwd);

    // Add a local modification to the module file, that suffices for testing
    // this case.
    $content = file_get_contents($workdir . '/ultimate_cron/ultimate_cron.module');
    $content .= "\$var = \"Local modification.\";\n";
    file_put_contents($workdir . '/ultimate_cron/ultimate_cron.module', $content);

    // Tearoff the patches and check that they're gone.
    $this->drush('bandaid-tearoff', array('ultimate_cron'), array(), NULL, $workdir);
    $this->assertEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/ultimate_cron'));

    $local_patch = $workdir . '/ultimate_cron.local.patch';
    // Ensure that we got a local patch file and it contains the expected.
    $this->assertFileExists($local_patch);

    $expected_diff = "diff --git a/ultimate_cron.module b/ultimate_cron.module\nindex 1cdf5b0..31daeaa 100755\n--- a/ultimate_cron.module\n+++ b/ultimate_cron.module\n@@ -1402,3 +1402,4 @@ function _ultimate_cron_default_settings(\$default_rule = NULL) {\n     'queue_lease_time' => '',\n   );\n }\n+\$var = \"Local modification.\";\n";
    $this->assertEquals($expected_diff, file_get_contents($local_patch));

  }

  /**
   * Ensure that trailing whitespace isn't stripped from diffs.
   *
   * Regression test. Naively using drush_shell_exec to output the diff, means
   * that it passes thogth exec, which strips trailing whitespace.
   */
  public function testDiffLineEndings() {
    $workdir = $this->webroot() . '/sites/all/modules';
    $this->drush('dl', array('exif_custom-1.13'), array(), NULL, $workdir);

    // Apply a patch, and check for success.
    $options = array(
      'home' => 'https://drupal.org/node/2112241',
      'reason' => 'Allow for overriding when uploading multiple images.',
    );
    $patch1_string = 'if(arg(3) == \'edit-multiple\'){return;}';
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));
    $this->drush('bandaid-patch', array('https://drupal.org/files/exif_override_multiple_images-2112241-1.patch', 'exif_custom'), $options, NULL, $workdir);
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));

    // We should have a yaml file now.
    $this->assertFileExists($workdir . '/exif_custom.yml');

    // And that the patch was added.
    $this->assertFileContains($workdir . '/exif_custom.yml', 'https://drupal.org/files/exif_override_multiple_images-2112241-1.patch');

    // Add a local modification with trailing whitespace to the module file
    // (we're prepending as they're happening too much at the end of the file).
    $content = file_get_contents($workdir . '/exif_custom/exif_custom.module');
    $content = "\$var = \"Local modification.\";   \n" . $content;
    file_put_contents($workdir . '/exif_custom/exif_custom.module', $content);

    $expected_diff = "diff --git a/exif_custom.module b/exif_custom.module\nindex c2bdee6..616306c 100644\n--- a/exif_custom.module\n+++ b/exif_custom.module\n@@ -1,3 +1,4 @@\n+\$var = \"Local modification.\";   \n <?php\n \n /**\n";

    // Do a diff to a file and check that it is as expected and the files
    // haven't changed..
    $diff_file = tempnam($workdir, 'patch_');
    $this->drush('bandaid-diff', array('exif_custom', $diff_file), array(), NULL, $workdir, self::EXIT_CODE_DIFF_DETECTED);
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/exif_custom'));

    $this->assertFileExists($diff_file);
    $this->assertEquals($expected_diff, file_get_contents($diff_file));

    // Tearoff the patches and check that they're gone.
    $this->drush('bandaid-tearoff', array('exif_custom'), array(), NULL, $workdir);
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));
    $this->assertEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/exif_custom'));

    $local_patch = $workdir . '/exif_custom.local.patch';
    // Ensure that we got a local patch file and it contains the expected.
    $this->assertFileExists($local_patch);
    $this->assertEquals($expected_diff, file_get_contents($local_patch));

    // Upgrade exif_custom.
    $this->drush('dl', array('exif_custom-1.14'), array('y' => TRUE), NULL, $workdir);

    // Reapply patches.
    $this->drush('bandaid-apply', array('exif_custom'), array(), NULL, $workdir);

    // The local patch file should be gone.
    $this->assertFalse(file_exists($local_patch));

    // And the project should contain the contents of the patches.
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/exif_custom'));
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/exif_custom'));
  }

  /**
   * Test that it works on core too.
   */
  public function testCoreFunctionallity() {
    $drupal_dir = $this->webroot();

    // Apply a patch, and check for success.
    $options = array(
      'home' => 'https://www.drupal.org/node/2249025',
      'reason' => 'MAINTAINERS.txt update',
    );

    // Make a directory to fake an module. This should survive the process.
    mkdir($drupal_dir . '/sites/all/modules/banana');
    file_put_contents($drupal_dir . '/sites/all/modules/banana/banana.module', 'fake');

    $patch1_string = 'The Drupal security team provides Security Advisories for vulnerabilities';
    $this->assertEmpty($this->grep($patch1_string, $drupal_dir . '/MAINTAINERS.txt'));
    $this->drush('bandaid-patch', array('https://www.drupal.org/files/issues/secteam-2249025-11.patch'), $options, NULL, $drupal_dir);
    $this->assertNotEmpty($this->grep($patch1_string, $drupal_dir . '/MAINTAINERS.txt'));

    // Check that our "module" still exists.
    $this->assertFileExists($drupal_dir . '/sites/all/modules/banana');
    $this->assertFileContains($drupal_dir . '/sites/all/modules/banana/banana.module', 'fake');

    // We should have a yaml file now.
    $this->assertFileExists($drupal_dir . '/core.yml');

    // And that the patch was added.
    $this->assertFileContains($drupal_dir . '/core.yml', 'https://www.drupal.org/files/issues/secteam-2249025-11.patch');

    // Add a local modification to the module file.
    $content = file_get_contents($drupal_dir . '/modules/user/user.module');
    $content .= "\$var = \"Local modification.\";\n";
    file_put_contents($drupal_dir . '/modules/user/user.module', $content);

    $expected_diff = "diff --git a/modules/user/user.module b/modules/user/user.module\nindex b239799..eb1215b3 100644\n--- a/modules/user/user.module\n+++ b/modules/user/user.module\n@@ -4027,3 +4027,4 @@ function user_system_info_alter(&\$info, \$file, \$type) {\n     \$info['hidden'] = FALSE;\n   }\n }\n+\$var = \"Local modification.\";\n";

    // Do a diff an check that it's the expected, and that the files haven't
    // changed.
    $this->drush('bandaid-diff', array(), array(), NULL, $drupal_dir, self::EXIT_CODE_DIFF_DETECTED);
    $this->assertEquals(trim($expected_diff), trim($this->getOutput()));
    $this->assertNotEmpty($this->grep($patch1_string, $drupal_dir . '/MAINTAINERS.txt'));
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $drupal_dir . '/modules/user/user.module'));

    // Tearoff the patches and check that they're gone.
    $this->drush('bandaid-tearoff', array(), array(), NULL, $drupal_dir);
    $this->assertEmpty($this->grep($patch1_string, $drupal_dir . '/MAINTAINERS.txt'));
    $this->assertEmpty($this->grep('\$var = \"Local modification.\";', $drupal_dir . '/modules/user/user.module'));

    $local_patch = $drupal_dir . '/core.local.patch';
    // Ensure that we got a local patch file and it contains the expected.
    $this->assertFileExists($local_patch);
    $this->assertEquals($expected_diff, file_get_contents($local_patch));

    // Check that our "module" still exists.
    $this->assertFileExists($drupal_dir . '/sites/all/modules/banana');
    $this->assertFileContains($drupal_dir . '/sites/all/modules/banana/banana.module', 'fake');

    // We'll skip the upgrading here. The other tests should catch most
    // breakage, so we'll go easy on core.

    // Reapply patches.
    $this->drush('bandaid-apply', array(), array(), NULL, $drupal_dir);
    // The local patch file should be gone.
    $this->assertFalse(file_exists($local_patch));

    // And the patches should be reapplied.
    $this->assertNotEmpty($this->grep($patch1_string, $drupal_dir . '/MAINTAINERS.txt'));
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $drupal_dir . '/modules/user/user.module'));

    // Check that our "module" still exists.
    $this->assertFileExists($drupal_dir . '/sites/all/modules/banana');
    $this->assertFileContains($drupal_dir . '/sites/all/modules/banana/banana.module', 'fake');
  }

  /**
   * Check that bandaid-diff exists with correct exit codes.
   *
   * We expect 0 at no diff and 1 when a difference exists.
   */
  public function testDiffExitcode(){
    $workdir = $this->webroot() . '/sites/all/modules';
    $this->drush('dl', array('exif_custom-1.13'), array(), NULL, $workdir);

    // Check that a diff on a clean download returns with an exit-code of 0.
    $this->drush('bandaid-diff', array('exif_custom'), array(), NULL, $workdir, self::EXIT_SUCCESS);

    // Do a quick local modification.
    $content = file_get_contents($workdir . '/exif_custom/exif_custom.module');
    $content = "\$var = \"Local modification.\";   \n" . $content;
    file_put_contents($workdir . '/exif_custom/exif_custom.module', $content);

    // Check that we get an exit-code of 1 (EXIT_CODE_DIFF_DETECTED) when we
    // have a diff.
    $this->drush('bandaid-diff', array('exif_custom'), array(), NULL, $workdir, self::EXIT_CODE_DIFF_DETECTED);
  }

  public function testGitRepo() {
    $workdir = $this->webroot() . '/sites/all/modules';
    $cwd = getcwd();
    chdir($workdir);
    // Using a non-drupal.org project.
    $this->execute('git clone https://github.com/Biblioteksvagten/ask_vopros.git');
    chdir('ask_vopros');
    // Check out some revision.
    $this->execute('git checkout 6106220e8cfca7a6911a71ac147d19d27cec8b63');
    chdir($workdir);

    // Ungit it.
    $this->drush('bandaid-degit', array('ask_vopros'), array('y' => TRUE), NULL, $workdir);

    // Ensure that the git dir has been removed.
    $this->assertFalse(file_exists('ask_vopros/.git'));

    // Add a local modification to the module file.
    $content = file_get_contents($workdir . '/ask_vopros/ask_vopros.admin.inc');
    $content .= "\$var = \"Local modification.\";\n";
    file_put_contents($workdir . '/ask_vopros/ask_vopros.admin.inc', $content);

    // Check that we get the expected diff.
    $expected_diff = "diff --git a/ask_vopros.admin.inc b/ask_vopros.admin.inc\nindex 2cb7a9e..3ce0f23 100644\n--- a/ask_vopros.admin.inc\n+++ b/ask_vopros.admin.inc\n@@ -94,3 +94,4 @@ function ask_vopros_settings(\$form, &\$form_state) {\n \n   return system_settings_form(\$form);\n }\n+\$var = \"Local modification.\";\n";

    $this->drush('bandaid-diff', array('ask_vopros'), array(), NULL, $workdir, self::EXIT_CODE_DIFF_DETECTED);
    $this->assertEquals(trim($expected_diff), trim($this->getOutput()));
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/ask_vopros'));

    // Tearoff the local changes and check that they're gone.
    $this->drush('bandaid-tearoff', array('ask_vopros'), array(), NULL, $workdir);
    $this->assertEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/ask_vopros'));

    $local_patch = $workdir . '/ask_vopros.local.patch';
    // Ensure that we got a local patch file and it contains the expected.
    $this->assertFileExists($local_patch);
    $this->assertEquals($expected_diff, file_get_contents($local_patch));

    // Regit it.
    $this->drush('bandaid-regit', array('ask_vopros'), array('y' => TRUE), NULL, $workdir);

    // Ensure that the git dir exists.
    $this->assertTrue(file_exists('ask_vopros/.git'));

    chdir('ask_vopros');
    // Check out a higher rev.
    $this->execute('git checkout e7f07d2774ec77dfde013d1891de41dd837cf516');
    chdir($workdir);

    // Degit again.
    $this->drush('bandaid-degit', array('ask_vopros'), array('y' => TRUE), NULL, $workdir);

    // Do an apply.
    // Reapply patches.
    $this->drush('bandaid-apply', array('ask_vopros'), array(), NULL, $workdir);
    // The local patch file should be gone.
    $this->assertFalse(file_exists($local_patch));

    // And the patches should be reapplied.
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/ask_vopros/ask_vopros.admin.inc'));

    // Check that we get the expected diff still.
    $expected_diff = "diff --git a/ask_vopros.admin.inc b/ask_vopros.admin.inc\nindex 2cb7a9e..3ce0f23 100644\n--- a/ask_vopros.admin.inc\n+++ b/ask_vopros.admin.inc\n@@ -94,3 +94,4 @@ function ask_vopros_settings(\$form, &\$form_state) {\n \n   return system_settings_form(\$form);\n }\n+\$var = \"Local modification.\";\n";

    $this->drush('bandaid-diff', array('ask_vopros'), array(), NULL, $workdir, self::EXIT_CODE_DIFF_DETECTED);
    $this->assertEquals(trim($expected_diff), trim($this->getOutput()));
    $this->assertNotEmpty($this->grep('\$var = \"Local modification.\";', $workdir . '/ask_vopros'));

    chdir($cwd);
  }

  /**
   * Grep for a string.
   */
  protected function grep($string, $root) {
    exec('grep -r ' . escapeshellarg($string) . ' ' . escapeshellarg($root), $output, $rc);
    if ($rc > 1) {
      $this->fail("Error grepping.");
    }
    return implode("\n", $output);
  }

  /**
   * Assert that file contains a given string.
   */
  protected function assertFileContains($file, $string) {
    $this->assertContains($string, file_get_contents($file));
  }

  /**
   * Assert that file contains a given string.
   */
  protected function assertFileNotContains($file, $string) {
    $this->assertNotContains($string, file_get_contents($file));
  }
}

class BandaidVersionParsingCase extends UnitUnishTestCase {
  /**
   * Setup. Load command file.
   */
  public static function setUpBeforeClass() {
    parent::setUpBeforeClass();
    require_once dirname(__DIR__) . '/bandaid.drush.inc';
  }
  /**
   * Test version parsing function.
   */
  public function testVersionParsing() {
    // 7.x-1.4 7.x-1.4+3-dev 7.x-2.0-alpha8+33-dev 7.x-1.x-dev
    $tests = array(
      '7.x-1.4' => array(
        'core' => '7.x',
        'major' => '1',
        'commits' => '',
        'version' => '1.4',
      ),
      '7.x-1.4+3-dev' => array(
        'core' => '7.x',
        'major' => '1',
        'commits' => '3',
        'version' => '1.4',
      ),
      '7.x-2.0-alpha8+33-dev' => array(
        'core' => '7.x',
        'major' => '2',
        'commits' => '33',
        'version' => '2.0-alpha8',
      ),
      // Full-length SHA.
      '60d9f28801533fecc92216a60d444d89d80e7611' => array(
        'sha' => '60d9f28801533fecc92216a60d444d89d80e7611',
      ),
      // Minimum-length (12) SHA.
      '60d9f2880153' => array(
        'sha' => '60d9f2880153',
      ),
    );

    foreach ($tests as $version => $parsed) {
      $this->assertEquals($parsed, _bandaid_parse_version($version));
    }

    // Test version strings that should fail (a dev-version without patch-level
    // a 11 char SHA, and a sha with non-hex chars.
    $bad_tests = array('7.x-1.x-dev', '60d9f288015', 'badsha8801xxxx5');
    foreach ($bad_tests as $version) {
      try {
        _bandaid_parse_version($version);
        $this->fail("Didn't get an exception on " . $version);
      }
      catch (Exception $e) {
        $this->assertInstanceOf('\\Bandaid\\BandaidError', $e);
      }
    }
  }
}

class BandaidIssuePatchesCase extends UnitUnishTestCase {
  /**
   * Setup. Load command file.
   */
  public static function setUpBeforeClass() {
    parent::setUpBeforeClass();
    require_once dirname(__DIR__) . '/bandaid.drush.inc';
    // Trigger loading of vendor libs.
    bandaid_drush_command();
  }
  /**
   * Test that parsing patches from issue URLs work.
   */
  public function testIssueParsing() {
    $tests = array(
      'https://www.drupal.org/node/2242071' => array(
        0 => array(
          'num' => 'node',
          'cid' => 0,
          'href' => 'https://www.drupal.org/files/issues/add-git-check-ignore-option.patch',
        ),
        8792979 => array(
          'num' => '2',
          'cid' => '8792979',
          'href' => 'https://www.drupal.org/files/issues/add-git-check-ignore-options-with-drush-scan-directory.patch',
        ),
        8793767 => array(
          'num' => '3',
          'cid' => '8793767',
          'href' => 'https://www.drupal.org/files/issues/drush_situs-git-check-ignore-2242071-3.patch',
        ),
        8795059 => array(
          'num' => '9',
          'cid' => '8795059',
          'href' => 'https://www.drupal.org/files/issues/drush_situs-git-check-ignore-2242071-9.patch',
        ),
      ),

      'https://www.drupal.org/node/1433906' => array(
        0 => array(
          'num' => 'node',
          'cid' => 0,
          'href' => 'https://www.drupal.org/files/devel-support.patch',
        ),
      ),

      'https://www.drupal.org/node/2133205' => array(
        8164173 => array(
          'num' => '1',
          'cid' => '8164173',
          'href' => 'https://www.drupal.org/files/issues/drush_situs-Also_ignore_README-2133205-1.patch',
        ),
      ),
    );

    foreach ($tests as $url => $parsed) {
      $res = _bandaid_get_patches_from_issue($url);
      $this->assertEquals($parsed, $res);
    }

  }
}
