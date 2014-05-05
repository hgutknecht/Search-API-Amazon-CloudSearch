<?php

/**
 * @file
 * aws_helpder.inc
 * Simple helper class for use with CloudSearch.
 */

class AWSCloudSearchHelper {

  /**
   * Creates an instance of the AmazonCloudSearch class.
   *
   * Given security credentials and AWS region.
   */
  public static function getAWSCloudSearchInstance($key, $secret, $region) {
    $library = libraries_load('awssdk2');
    if (!$library['loaded']) {
      throw new Exception(t('Unable to load the AWS SDK. Please ensure that the awssdk2 library is installed correctly.'));
    }
    elseif (!class_exists('Aws\S3\S3Client')) {
      throw new Exception(t('Cannot load Aws\S3\S3Client class. Please ensure that the awssdk2 library is installed correctly.'));
    }

    $client = Aws\CloudSearch\CloudSearchClient::factory(array(
      'key' => $key,
      'secret' => $secret,
      'region'  => $region
      )
    );

    return $client;
  }
}
