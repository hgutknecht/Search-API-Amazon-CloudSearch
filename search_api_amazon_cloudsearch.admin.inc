<?php

/**
 * @file
 * search_api_amazon_cloudsearch.admin.inc
 */

/**
 * Form constructor to CloudSearch specific settings.
 *
 * This form allows the user to specify which fields they would like to
 * be sortable and available for ranged queryies (<=, >= etc).
 * There are a few restrictions in CloudSearch index fields that mean that
 * additional fields need to be created.
 */
function search_api_amazon_cloudsearch_index_setup($form, &$form_state, SearchApiIndex $index) {
  module_load_include('admin.inc', 'search_api');
  drupal_set_title(search_api_admin_item_title($index));
  $form_state['index'] = $index;

  $server = $index->server();

  // Check to make sure this index is using the CloudSearch server class.
  if ($server == NULL || $server->class != 'search_api_cloudsearch_service') {
    $form['not_valid'] = array(
      '#type' => 'markup',
      '#markup' => t('This index is not set to use the CloudSearch server.'),
    );
    return $form;
  }

  $options = $server->options;

  $form['intro'] = array(
    '#type' => 'markup',
    '#markup' => '<p>' . t('In order to facilitate some advanced Search API features
    CloudSearch needs to make copies of certain fields.  To keep the number
    of indexes within CloudSearch to a minimum please specifiy which fields
    you would like to be sortable and/or available for ranged queries.') . '</p>' . '<p>' .
    t('Note that these settings do not have any impact on the standard Search API settings.') . '</p>',
  );

  $fields = array();
  $range_fields = array();
  if (!empty($index->options['fields'])) {
    foreach ($index->options['fields'] as $key => $field) {
      $fields[$key] = $key;
      if (AWSCloudSearchDomain::isNumericFieldType($field['type'])) {
        $range_fields[$key] = $key;
      }
    }
  }

  // Get a list of content types to drive the exclude checkbox list and
  // transform into an associative array
  $content_types = array_reduce(
    node_type_get_types(),
    function($result, $ct) {
      $result[$ct->type] = $ct->type;
      return $result;
    }
  );

  $form['vtab'] = array(
    '#type' => 'vertical_tabs',
    '#title' => 'Amazon CloudSearch field configuration options',
  );

  $form['fieldset_sort_params'] = array(
    '#type' => 'fieldset',
    '#group' => 'vtab',
    '#title' => t('Results ranking'),
    '#description' => t('Settings related to adjusting the way results are returned.'),
  );

  $form['fieldset_sort_params']['sort_expression'] = array(
    '#type'  => 'textfield',
    '#title'  => t('Sort expression'),
    '#description' => t('Specify a predefined sort expression to use for results sorting.'),
    '#default_value' => !empty($options['index_options'][$index->machine_name]['sort_expression'])
      ? $options['index_options'][$index->machine_name]['sort_expression']
      : '',
  );

   $sort_opts = array(0 => t('Descending'), 1 => t('Ascending'));

   $form['fieldset_sort_params']['sort_order'] = array(
    '#type' => 'radios',
    '#title' => t('Sort order'),
    '#default_value' => !empty($options['index_options'][$index->machine_name]['sort_order'])
      ? $options['index_options'][$index->machine_name]['sort_order']
      : 0,
    '#options' => $sort_opts,
    '#description' => t('Results sort order.'),
  );

  $form['fieldset_sorts'] = array(
    '#type' => 'fieldset',
    '#group' => 'vtab',
    '#title' => t('Sortable fields'),
    '#description' => t('Choose which fields should be sortable within the index.'),
  );
  $form['fieldset_ranges'] = array(
    '#type' => 'fieldset',
    '#group' => 'vtab',
    '#title' => t('Ranged fields'),
    '#description' => t('Choose which number fields should be able to be filtered by range (e.g. field_age 25 - 35).'),
  );

  $form['fieldset_sorts']['sorts'] = array(
    '#type' => 'checkboxes',
    '#title' => t('Sortable fields'),
    '#options' => $fields,
    '#multiple' => true,
    '#default_value' => !empty($options['index_options'][$index->machine_name]['sorts']) ? $options['index_options'][$index->machine_name]['sorts'] : array(),
    '#size' => 8,
  );

  $form['fieldset_ranges']['ranges'] = array(
    '#type' => 'checkboxes',
    '#title' => t('Rangable fields'),
    '#options' => $range_fields,
    '#multiple' => true,
    '#default_value' => !empty($options['index_options'][$index->machine_name]['sorts']) ? $options['index_options'][$index->machine_name]['ranges'] : array(),
    '#size' => 8,
  );

  $form['fieldset_exclude'] = array(
    '#type' => 'fieldset',
    '#group' => 'vtab',
    '#title' => t('Excluded content'),
    '#description' => t('Content types to exclude from indexing.'),
  );

  $form['fieldset_exclude']['excluded_content_types'] = array(
    '#type' => 'checkboxes',
    '#title' => t('Excluded content types'),
    '#options' => $content_types,
    '#multiple' => true,
    '#default_value' => !empty($options['index_options'][$index->machine_name]['excluded_content_types']) ? $options['index_options'][$index->machine_name]['excluded_content_types'] : array(),
    '#size' => 8,
  );

  $form['submit_warning'] = array(
    '#type' => 'markup',
    '#markup' => '<p class="warning">' . t('Making changes to the configuration here will update the CloudSearch domain.
      Any changes to entities while the domain is processing will sit in the queue until the processing is complete. Searching will continue to function.') . '</p>',
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save configuration'),
  );

  return $form;
}

/**
 * Submit handler for CloudSearch specific settings.
 *
 * Saves the preferences in the server options array and
 * issues an update of indexes for the specific index. The
 * update indexes request may add additional fields, if so
 * then it will take some time for the new indexes to be processed.
 */
function search_api_amazon_cloudsearch_index_setup_submit($form, &$form_state) {
  $index = $form_state['index'];
  $options = $index->server()->options;

  if (!empty($form_state['values']['sorts'])) {
    $options['index_options'][$index->machine_name]['sorts'] = $form_state['values']['sorts'];
  }
  if (!empty($form_state['values']['ranges'])) {
    $options['index_options'][$index->machine_name]['ranges'] = $form_state['values']['ranges'];
  }
  if (!empty($form_state['values']['excluded_content_types'])) {
    $options['index_options'][$index->machine_name]['excluded_content_types'] = $form_state['values']['excluded_content_types'];
  }
  if (!empty($form_state['values']['sort_expression'])) {
    $options['index_options'][$index->machine_name]['sort_expression'] = $form_state['values']['sort_expression'];
  }
  if (isset($form_state['values']['sort_order'])) {
    $options['index_options'][$index->machine_name]['sort_order'] = $form_state['values']['sort_order'];
  }

  $index->server()->update(array('options' => $options));

  $csdomain = new AWSCloudSearchDomain($options, $index->machine_name);
  $csdomain->updateIndexes($index);
  drupal_set_message(t('CloudSearch indexes were updated. It can take up to 10 minutes for the indexes to be processed. During this time no items can be added to CloudSearch'));
}
