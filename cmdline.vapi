/* gengetopt.vapi
 *
 * Copyright (C) 2023 Reuben Thomas <rrt@sc3d.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

[CCode (cprefix = "cmdline", destroy_function = "", cheader_filename = "cmdline-vala.h")]
namespace Gengetopt {
	[CCode (cname = "gengetopt_args_info", destroy_function = "", has_type_id = false)]
	public struct ArgsInfo
	{
		const string help_help; /**< @brief Print help and exit help description.  */
		const string version_help; /**< @brief Print version and exit help description.  */
		int hidden_flag;	/**< @brief treat dot files normally (default=off).  */
		const string hidden_help; /**< @brief treat dot files normally help description.  */
		int makedirs_flag;	/**< @brief create non-existent directories (default=off).  */
		const string makedirs_help; /**< @brief create non-existent directories help description.  */
		const string move_help; /**< @brief move source file to target name help description.  */
		const string copydel_help; /**< @brief copy source to target, then delete source help description.  */
		const string rename_help; /**< @brief rename source to target in same directory help description.  */
		const string copy_help; /**< @brief copy source to target, preserving source permissions help description.  */
		const string overwrite_help; /**< @brief overwrite target with source, preserving target permissions help description.  */
		const string hardlink_help; /**< @brief link target name to source file help description.  */
		const string symlink_help; /**< @brief symlink target name to source file help description.  */
		const string force_help; /**< @brief perform file deletes and overwrites without confirmation help description.  */
		const string protect_help; /**< @brief treat file deletes and overwrites as errors help description.  */
		const string go_help; /**< @brief skip any erroneous actions help description.  */
		const string terminate_help; /**< @brief erroneous actions are treated as errors help description.  */
		const string verbose_help; /**< @brief report all actions performed help description.  */
		const string dryrun_help; /**< @brief only report which actions would be performed help description.  */

		uint help_given ;	/**< @brief Whether help was given.  */
		uint version_given ;	/**< @brief Whether version was given.  */
		uint hidden_given ;	/**< @brief Whether hidden was given.  */
		uint makedirs_given ;	/**< @brief Whether makedirs was given.  */
		uint move_given ;	/**< @brief Whether move was given.  */
		uint copydel_given ;	/**< @brief Whether copydel was given.  */
		uint rename_given ;	/**< @brief Whether rename was given.  */
		uint copy_given ;	/**< @brief Whether copy was given.  */
		uint overwrite_given ;	/**< @brief Whether overwrite was given.  */
		uint append_given ;	/**< @brief Whether append was given.  */
		uint hardlink_given ;	/**< @brief Whether hardlink was given.  */
		uint symlink_given ;	/**< @brief Whether symlink was given.  */
		uint force_given ;	/**< @brief Whether force was given.  */
		uint protect_given ;	/**< @brief Whether protect was given.  */
		uint go_given ;	/**< @brief Whether go was given.  */
		uint terminate_given ;	/**< @brief Whether terminate was given.  */
		uint verbose_given ;	/**< @brief Whether verbose was given.  */
		uint dryrun_given ;	/**< @brief Whether dryrun was given.  */

		[CCode (array_length_cname = "inputs_num", array_length_type = "unsigned")]
		string[] inputs; /**< @brief unnamed options (options without names) */
		int delete_group_counter; /**< @brief Counter for group delete */
		int erroneous_group_counter; /**< @brief Counter for group erroneous */
		int mode_group_counter; /**< @brief Counter for group mode */
		int report_group_counter; /**< @brief Counter for group report */

		[CCode (cname = "cmdline_parser")]
		public static int parser([CCode (array_length_pos = 0.1)] string[] args, ref ArgsInfo args_info);

		[CCode (cname = "cmdline_parser_print_help")]
		public static void parser_print_help();
	}
}
